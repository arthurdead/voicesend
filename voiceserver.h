//from https://github.com/NiDE-gg/sm-ext-voice

#pragma once

#include "smsdk_ext.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include "ringbuffer.h"
#include <string>
#include "voicecodec_celt.h"

#define MAX_CLIENTS 16

class VoiceServer
{
public:
	VoiceServer(std::string &&addr, int port);
	~VoiceServer();

	static void OnGameFrame(bool simulating);
	void ListenSocket();
	void Init();

	void Unload();

	Handle_t handle;

	void SetSettings(celt_int32 SampleRate_Hz, celt_int32 FrameSize, celt_int32 PacketSize);

	float volume = 1.0;

private:
	struct CClient
	{
		int m_Socket;
		size_t m_BufferWriteIndex;
		size_t m_LastLength;
		double m_LastValidData;
		bool m_New;
		bool m_UnEven;
		unsigned char m_Remainder;
	};

	void HandleNetwork();
	void OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples);
	void HandleVoiceData();

	int m_ListenSocket;

	CClient m_aClients[MAX_CLIENTS];

	struct pollfd m_aPollFds[1 + MAX_CLIENTS];
	int m_PollFds;

	CRingBuffer m_Buffer;

	double m_AvailableTime;

	VoiceCodec_Celt::CEncoderSettings m_EncoderSettings;
	std::string addr;
	int port;
};
