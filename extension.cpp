/**
 * vim: set ts=4 :
 * =============================================================================
 * SourceMod Sample Extension
 * Copyright (C) 2004-2008 AlliedModders LLC.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

#include "extension.h"
#include <string>
#include <string_view>
#include <filesystem>
#include <unordered_map>
#include <dlfcn.h>
#include <CDetour/detours.h>
#include <iclient.h>
#include <iserver.h>
#include "ivoicecodec.h"
#include <tier1/interface.h>
#include "netmessages.h"
#include "voicecodec_celt.h"

/**
 * @file extension.cpp
 * @brief Implement extension code here.
 */

Sample g_Sample;		/**< Global singleton for extension's main interface */

SMEXT_LINK(&g_Sample);

ICvar *g_pCVar;
IServer *sv;
ConVar *sv_voicecodec;
ConVar *sv_use_steam_voice;
ConVar *sv_voiceenable;
ConVar *voice_debugfeedbackfrom;
HandleType_t voicecodec_handle;
IForward *OnVoiceInit;
IForward *OnVoiceData;
struct codecdl
{
	CSysModule *dl;
	CreateInterfaceFn func;
};
std::unordered_map<std::string, codecdl> dlmap;

inline int Voice_GetDefaultSampleRate( const char *pCodec ) // Inline for DEDICATED builds
{
	// Use legacy lower rate for speex
	if ( Q_stricmp( pCodec, "vaudio_speex" ) == 0 )
	{
		return 11025;
	}
	else if ( Q_stricmp( pCodec, "steam" ) == 0 )
	{
		return 0; // For the steam codec, 0 passed to voice_init means "use optimal steam voice rate"
	}
	else if ( Q_stricmp( pCodec, "vaudio_celt" ) == 0 )
	{
		return 22050;
	}
	else if ( Q_stricmp( pCodec, "vaudio_celt_high" ) == 0 )
	{
		return 44100;
	}

	// Use high sample rate by default for other codecs.
	return 22050;
}

static void clamp_samplerate(int &nSampleRate)
{
	if(nSampleRate >= 22050) {
		nSampleRate = 22050;
	} else if(nSampleRate >= 11025) {
		nSampleRate = 11025;
	} else if(nSampleRate == 0) {
		nSampleRate = 0;
	} else if(nSampleRate < 11025) {
		nSampleRate = 11025;
	}
}

CDetour *SV_WriteVoiceCodec_detour;
DETOUR_DECL_STATIC1(SV_WriteVoiceCodec, void, bf_write &, pBuf)
{
	const char *pCodec{sv_voicecodec->GetString()};

	cell_t nSampleRate{Voice_GetDefaultSampleRate(pCodec)};

	char szVoiceCodec[MAX_OSPATH]{'\0'};
	V_strncpy(szVoiceCodec, pCodec, sizeof(szVoiceCodec));

	OnVoiceInit->PushStringEx(szVoiceCodec, sizeof(szVoiceCodec), SM_PARAM_STRING_COPY|SM_PARAM_STRING_UTF8, SM_PARAM_COPYBACK);
	OnVoiceInit->PushCell(MAX_OSPATH);
	OnVoiceInit->PushCellByRef(&nSampleRate);
	OnVoiceInit->Execute();

	clamp_samplerate(nSampleRate);

	SVC_VoiceInit voiceinit{szVoiceCodec, nSampleRate};
	voiceinit.WriteToBuffer(pBuf);
}

static cell_t SendVoiceInit(IPluginContext *pContext, const cell_t *params)
{
	const int client{params[1]};
	char *pCodec;
	pContext->LocalToString(params[2], &pCodec);
	int nSampleRate{params[3]};

	clamp_samplerate(nSampleRate);

	IClient *cl{sv->GetClient(client-1)};

	SVC_VoiceInit voiceinit{pCodec, nSampleRate};

	cl->SendNetMsg(voiceinit);

	return 0;
}

CDetour *SV_BroadcastVoiceData_detour;
DETOUR_DECL_STATIC4(SV_BroadcastVoiceData, void, IClient *, pClient, int, nBytes, char *, data, int64, xuid)
{
	// Disable voice?
	if( !sv_voiceenable->GetInt() )
		return;

	// Build voice message once
	SVC_VoiceData voiceData;
	voiceData.m_nFromClient = pClient->GetPlayerSlot();
	voiceData.m_nLength = nBytes * 8;	// length in bits
	voiceData.m_DataOut = data;
	voiceData.m_xuid = xuid;

	if ( voice_debugfeedbackfrom->GetBool() )
	{
		Msg( "Sending voice from: %s - playerslot: %d\n", pClient->GetClientName(), pClient->GetPlayerSlot() + 1 );
	}

	for(int i=0; i < sv->GetClientCount(); i++)
	{
		IClient *pDestClient = sv->GetClient(i);
		if(!pDestClient->IsActive()) {
			continue;
		}

		voiceData.m_nFromClient = pClient->GetPlayerSlot();

		bool bSelf = (pDestClient == pClient);
		bool bHearsPlayer = pDestClient->IsHearingClient(voiceData.m_nFromClient);

		if(!bHearsPlayer && !bSelf) {
			continue;
		}

		cell_t sender{pClient->GetPlayerSlot()+1};
		cell_t proximity{pDestClient->IsProximityHearingClient(voiceData.m_nFromClient)};

		voiceData.m_bProximity = proximity;

		OnVoiceData->PushCell(sender);
		OnVoiceData->PushCell(i+1);
		OnVoiceData->PushStringEx(data, nBytes, SM_PARAM_STRING_COPY|SM_PARAM_STRING_BINARY, SM_PARAM_COPYBACK);
		OnVoiceData->PushCell(nBytes);
		OnVoiceData->PushCellByRef(&proximity);
		OnVoiceData->Execute(nullptr);

		voiceData.m_bProximity = proximity;

		if(!bHearsPlayer) {
			voiceData.m_nLength = 0;
		} else {
			voiceData.m_nLength = nBytes * 8;
		}

		pDestClient->SendNetMsg( voiceData );
	}
}

static cell_t SendVoiceData(IPluginContext *pContext, const cell_t *params)
{
	const int client{params[1]};
	char *data;
	pContext->LocalToString(params[2], &data);
	const int len{params[3]};
	const int from{params[4]};
	const bool proximity{static_cast<bool>(params[5])};

	IClient *cl{sv->GetClient(client-1)};

	SVC_VoiceData voicedata;
	voicedata.m_nFromClient = from;
	voicedata.m_bProximity = proximity;
	voicedata.m_nLength = (len * 8);
	voicedata.m_xuid = 0;
	voicedata.m_DataOut = data;

	cl->SendNetMsg(voicedata);

	return 0;
}

static cell_t handle_createvoicecodec(IPluginContext *pContext, const cell_t *params, bool ex)
{
	using namespace std::literals::string_view_literals;

	char *name_ptr;
	pContext->LocalToString(params[1], &name_ptr);
	std::string_view name{name_ptr};

	if(name == "voicesend_celt"sv) {
		VoiceCodec_Celt *codec = new VoiceCodec_Celt();
		return handlesys->CreateHandle(voicecodec_handle, codec, pContext->GetIdentity(), myself->GetIdentity(), NULL);
	}

	CreateInterfaceFn func{nullptr};
	auto it{dlmap.find(std::string{name})};
	if(it == dlmap.end()) {
		std::string dlpath;
		dlpath += name;
		dlpath += ".so"sv;

		//CSysModule *dl{Sys_LoadModule(dlpath.c_str(), SYS_NOFLAGS)};
		CSysModule *dl{reinterpret_cast<CSysModule *>(dlopen(dlpath.c_str(), RTLD_NOW))};
		if(dl) {
			func = Sys_GetFactory(dl);
			if(func) {
				dlmap.emplace(std::pair<std::string,codecdl>{name,codecdl{dl,func}});
			} else {
				if(ex) {
					const int len{static_cast<cell_t>(params[3])};
					pContext->StringToLocal(params[2], len, "missing factory");
				}
				dlclose(dl);
			}
		} else {
			if(ex) {
				const int len{static_cast<cell_t>(params[3])};
				const char *err{dlerror()};
				if(!err) {
					err = "";
				}
				pContext->StringToLocal(params[2], len, err);
			}
		}
	} else {
		func = it->second.func;
	}

	if(func) {
		int status;
		using namespace std::literals::string_literals;
		std::string ifacename;
		ifacename += name;
		IVoiceCodec *codec{reinterpret_cast<IVoiceCodec *>(func(ifacename.data(), &status))};
		if(codec) {
			return handlesys->CreateHandle(voicecodec_handle, codec, pContext->GetIdentity(), myself->GetIdentity(), NULL);
		} else {
			if(ex) {
				const int len{static_cast<cell_t>(params[3])};
				pContext->StringToLocal(params[2], len, "factory returned null");
			}
		}
	}

	return 0;
}

static cell_t CreateVoiceCodec(IPluginContext *pContext, const cell_t *params)
{
	return handle_createvoicecodec(pContext, params, false);
}

static cell_t CreateVoiceCodecEx(IPluginContext *pContext, const cell_t *params)
{
	return handle_createvoicecodec(pContext, params, true);
}

static cell_t CreateCeltCodecEx(IPluginContext *pContext, const cell_t *params)
{
	int samplerate = params[1];
	int framesize = params[2];
	int packetsize = params[3];

	VoiceCodec_Celt *codec{new VoiceCodec_Celt{}};
	codec->Init(samplerate, framesize, packetsize);
	return handlesys->CreateHandle(voicecodec_handle, codec, pContext->GetIdentity(), myself->GetIdentity(), NULL);
}

static cell_t VoiceCodecInit(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());

	IVoiceCodec *obj = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], voicecodec_handle, &security, (void **)&obj);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}

	return static_cast<cell_t>(obj->Init(static_cast<int>(params[2])));
}

static cell_t VoiceCodecResetState(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());

	IVoiceCodec *obj = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], voicecodec_handle, &security, (void **)&obj);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}

	return static_cast<cell_t>(obj->ResetState());
}

static cell_t VoiceCodecCompress(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());

	IVoiceCodec *obj = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], voicecodec_handle, &security, (void **)&obj);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}

	char *pUncompressed;
	pContext->LocalToString(params[2], &pUncompressed);
	const int nSamples{static_cast<int>(params[3])};

	char *pCompressed;
	pContext->LocalToString(params[4], &pCompressed);
	int maxCompressedBytes{static_cast<int>(params[5])};

	const bool bFinal{static_cast<bool>(params[6])};

	const int ret{obj->Compress(pUncompressed, nSamples, pCompressed, maxCompressedBytes, bFinal)};

	return static_cast<cell_t>(ret);
}

static cell_t VoiceCodecDecompress(IPluginContext *pContext, const cell_t *params)
{
	HandleSecurity security(pContext->GetIdentity(), myself->GetIdentity());

	IVoiceCodec *obj = nullptr;
	HandleError err = handlesys->ReadHandle(params[1], voicecodec_handle, &security, (void **)&obj);
	if(err != HandleError_None)
	{
		return pContext->ThrowNativeError("Invalid Handle %x (error: %d)", params[1], err);
	}

	return -1;
}

static constexpr const sp_nativeinfo_t natives[]{
	{"SendVoiceData", SendVoiceData},
	{"SendVoiceInit", SendVoiceInit},
	{"CreateVoiceCodec", CreateVoiceCodec},
	{"CreateVoiceCodecEx", CreateVoiceCodecEx},
	{"CreateCeltCodecEx", CreateCeltCodecEx},
	{"VoiceCodec.Init", VoiceCodecInit},
	{"VoiceCodec.Compress", VoiceCodecCompress},
	{"VoiceCodec.Decompress", VoiceCodecDecompress},
	{"VoiceCodec.ResetState", VoiceCodecResetState},
	{nullptr, nullptr}
};

void Sample::OnHandleDestroy(HandleType_t type, void *object)
{
	if(type == voicecodec_handle) {
		IVoiceCodec *codec{reinterpret_cast<IVoiceCodec *>(object)};
		codec->ResetState();
		codec->Release();
	}
}

void OnGameFrame(bool simulating)
{
	
}

bool Sample::SDK_OnLoad(char *error, size_t maxlen, bool late)
{
	IGameConfig *gameconf;
	gameconfs->LoadGameConfigFile("voicesend", &gameconf, error, maxlen);

	CDetourManager::Init(smutils->GetScriptingEngine(), gameconf);

	SV_WriteVoiceCodec_detour = DETOUR_CREATE_STATIC(SV_WriteVoiceCodec, "SV_WriteVoiceCodec");
	SV_WriteVoiceCodec_detour->EnableDetour();

	SV_BroadcastVoiceData_detour = DETOUR_CREATE_STATIC(SV_BroadcastVoiceData, "SV_BroadcastVoiceData");
	SV_BroadcastVoiceData_detour->EnableDetour();

	gameconfs->CloseGameConfigFile(gameconf);

	voicecodec_handle = handlesys->CreateType("VoiceCodec", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);

	OnVoiceInit = forwards->CreateForward("OnVoiceInit", ET_Event, 3, nullptr, Param_String, Param_Cell, Param_CellByRef);
	OnVoiceData = forwards->CreateForward("OnVoiceData", ET_Event, 3, nullptr, Param_CellByRef, Param_String, Param_CellByRef);

	VoiceCodec_Celt::InitGlobalSettings();
	smutils->AddGameFrameHook(::OnGameFrame);

	sharesys->AddNatives(myself, natives);
	sharesys->RegisterLibrary(myself, "voicesend");

	return true;
}

void Sample::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(::OnGameFrame);
	for(auto &[name,dl] : dlmap) {
		Sys_UnloadModule(dl.dl);
	}
	forwards->ReleaseForward(OnVoiceInit);
	forwards->ReleaseForward(OnVoiceData);
	handlesys->RemoveType(voicecodec_handle, myself->GetIdentity());
	SV_WriteVoiceCodec_detour->Destroy();
	SV_BroadcastVoiceData_detour->Destroy();
}

bool Sample::RegisterConCommandBase(ConCommandBase *pVar)
{
	return META_REGCVAR(pVar);
}

bool Sample::SDK_OnMetamodLoad(ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	sv_voicecodec = g_pCVar->FindVar("sv_voicecodec");
	sv_use_steam_voice = g_pCVar->FindVar("sv_use_steam_voice");
	sv_voiceenable = g_pCVar->FindVar("sv_voiceenable");
	voice_debugfeedbackfrom = g_pCVar->FindVar("voice_debugfeedbackfrom");
	sv = engine->GetIServer();
	ConVar_Register(0, this);
	return true;
}
