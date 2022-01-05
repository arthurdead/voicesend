#include "voicecodec_celt.h"
#include "smsdk_ext.h"
#include <tier1/convar.h>

static VoiceCodec_Celt::CEncoderSettings globalEncoderSettings;

extern ConVar *sv_voicecodec;

void VoiceCodec_Celt::InitGlobalSettings()
{
	const char *pCodec{sv_voicecodec->GetString()};

	int default_sample_rate_hz{22050};
	int default_frame_size{512};
	int default_packet_size{64};

	if(strcmp(pCodec, "vaudio_celt") == 0) {
		default_sample_rate_hz = 22050;
		default_frame_size = 512;
		default_packet_size = 64;
	} else if(strcmp(pCodec, "vaudio_celt_high") == 0) {
		default_sample_rate_hz = 44100;
		default_frame_size = 256;
		default_packet_size = 120;
	} else if(strcmp(pCodec, "vaudio_speex") == 0) {
		default_sample_rate_hz = 8000;
		default_frame_size = 28;
		default_packet_size = 60;
	} else if(strcmp(pCodec, "steam") == 0) {
		default_sample_rate_hz = 44100;
		default_frame_size = 512;
		default_packet_size = 64;
	}

	globalEncoderSettings.SampleRate_Hz = default_sample_rate_hz;
	globalEncoderSettings.TargetBitRate_Kbps = 64;
	globalEncoderSettings.FrameSize = default_frame_size; // samples
	globalEncoderSettings.PacketSize = default_packet_size;
	globalEncoderSettings.Complexity = 10; // 0 - 10
	globalEncoderSettings.FrameTime = (double)globalEncoderSettings.FrameSize / (double)globalEncoderSettings.SampleRate_Hz;
}

bool VoiceCodec_Celt::Init( int quality )
{
	celt_int32 SampleRate_Hz = 0;
	celt_int32 FrameSize = 0;
	celt_int32 PacketSize = 0;

	switch(quality) {
		case 0: {
			SampleRate_Hz = 44100;
			FrameSize = 256;
			PacketSize = 120;
		}
		case 1: {
			SampleRate_Hz = 22050;
			FrameSize = 120;
			PacketSize = 60;
		}
		case 2: {
			SampleRate_Hz = 22050;
			FrameSize = 256;
			PacketSize = 60;
		}
		case 3: {
			SampleRate_Hz = 22050;
			FrameSize = 512;
			PacketSize = 64;
		}
	}

	return Init(SampleRate_Hz, FrameSize, PacketSize);
}

const VoiceCodec_Celt::CEncoderSettings &VoiceCodec_Celt::TheEncoderSettings()
{
	return globalEncoderSettings;
}

VoiceCodec_Celt::VoiceCodec_Celt()
{
	m_pMode = NULL;
	m_pCodec = NULL;
}

bool VoiceCodec_Celt::Init(celt_int32 SampleRate_Hz, celt_int32 FrameSize, celt_int32 PacketSize)
{
	m_EncoderSettings = globalEncoderSettings;

	if(SampleRate_Hz != 0) {
		m_EncoderSettings.SampleRate_Hz = SampleRate_Hz;
	}
	if(FrameSize != 0) {
		m_EncoderSettings.FrameSize = FrameSize;
	}
	if(PacketSize != 0) {
		m_EncoderSettings.PacketSize = PacketSize;
	}

	m_EncoderSettings.FrameTime = (double)m_EncoderSettings.FrameSize / (double)m_EncoderSettings.SampleRate_Hz;

	int theError;
	m_pMode = celt_mode_create(m_EncoderSettings.SampleRate_Hz, m_EncoderSettings.FrameSize, &theError);
	if(!m_pMode)
	{
		smutils->LogError(myself, "celt_mode_create error: %d", theError);
		return false;
	}

	m_pCodec = celt_encoder_create_custom(m_pMode, 1, &theError);
	if(!m_pCodec)
	{
		smutils->LogError(myself, "celt_encoder_create_custom error: %d", theError);
		return false;
	}

	celt_encoder_ctl(m_pCodec, CELT_RESET_STATE_REQUEST, NULL);
	celt_encoder_ctl(m_pCodec, CELT_SET_BITRATE(m_EncoderSettings.TargetBitRate_Kbps * 1000));
	celt_encoder_ctl(m_pCodec, CELT_SET_COMPLEXITY(m_EncoderSettings.Complexity));

	return true;
}

VoiceCodec_Celt::~VoiceCodec_Celt()
{
	if(m_pCodec)
		celt_encoder_destroy(m_pCodec);

	if(m_pMode)
		celt_mode_destroy(m_pMode);
}

void VoiceCodec_Celt::Release()
{
	delete this;
}

bool VoiceCodec_Celt::ResetState()
{
	celt_encoder_ctl(m_pCodec, CELT_RESET_STATE_REQUEST, NULL);
	return true;
}

int	VoiceCodec_Celt::Compress(celt_int16 *pUncompressed, int nSamples, unsigned char *pCompressed, int maxCompressedBytes)
{
	return celt_encode(m_pCodec, pUncompressed, nSamples, pCompressed, maxCompressedBytes);
}

int	VoiceCodec_Celt::Compress(const char *pUncompressed, int nSamples, char *pCompressed, int maxCompressedBytes, bool bFinal)
{
	return celt_encode(m_pCodec, (celt_int16 *)pUncompressed, nSamples, (unsigned char *)pCompressed, maxCompressedBytes);
}

int	VoiceCodec_Celt::Decompress(const char *pCompressed, int compressedBytes, char *pUncompressed, int maxUncompressedBytes)
{
	return -1;
}
