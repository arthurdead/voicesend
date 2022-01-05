#include "smsdk_ext.h"
#include "extension.h"
#include "voiceserver.h"
#include "voicecodec_celt.h"
#include <iclient.h>
#include <iserver.h>
#include "netmessages.h"

//#define DEBUG_CONNECTION

static std::vector<VoiceServer *> servers;

extern IServer *sv;
extern IForward *OnVoiceServerData;

template <typename T> inline T min(T a, T b) { return a<b?a:b; }

double getTime()
{
    struct timespec tv;
    if(clock_gettime(CLOCK_REALTIME, &tv) != 0)
    	return 0;

    return (tv.tv_sec + (tv.tv_nsec / 1000000000.0));
}

static void ListenSocketAction(void *pData)
{
	VoiceServer *pThis = (VoiceServer *)pData;
	pThis->ListenSocket();
}

VoiceServer::VoiceServer(std::string &&addr_, int port_)
	: addr{std::move(addr_)}, port{port_}
{
	servers.emplace_back(this);

	m_ListenSocket = -1;

	m_PollFds = 0;
	for(int i = 1; i < 1 + MAX_CLIENTS; i++)
		m_aPollFds[i].fd = -1;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aClients[i].m_Socket = -1;

	m_AvailableTime = 0.0;

	m_EncoderSettings = VoiceCodec_Celt::TheEncoderSettings();

	Init();
}

VoiceServer::~VoiceServer()
{
	servers.erase(std::remove(servers.begin(), servers.end(), this));

	Unload();
}

void VoiceServer::Init()
{
	m_Buffer.Init(32768);

	// Init tcp server
	m_ListenSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(m_ListenSocket < 0)
	{
		smutils->LogError(myself, "Failed creating socket.");
		Unload();
		return;
	}

	int yes = 1;
	if(setsockopt(m_ListenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0)
	{
		smutils->LogError(myself, "Failed setting SO_REUSEADDR on socket.");
		Unload();
		return;
	}

	// ... delay starting listen server to next frame
	smutils->AddFrameAction(ListenSocketAction, this);
}

void VoiceServer::SetSettings(celt_int32 SampleRate_Hz, celt_int32 FrameSize, celt_int32 PacketSize)
{
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
}

void VoiceServer::ListenSocket()
{
	if(m_PollFds > 0)
		return;

	sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	inet_aton(addr.c_str(), &bindAddr.sin_addr);
	bindAddr.sin_port = htons(port);

#ifdef DEBUG_CONNECTION
	smutils->LogMessage(myself, "Binding to %s:%d!\n", addr.c_str(), port);
#endif

	if(bind(m_ListenSocket, (sockaddr *)&bindAddr, sizeof(sockaddr_in)) < 0)
	{
		smutils->LogError(myself, "Failed binding to socket (%d '%s').", errno, strerror(errno));
		Unload();
		return;
	}

	if(listen(m_ListenSocket, MAX_CLIENTS) < 0)
	{
		smutils->LogError(myself, "Failed listening on socket.");
		Unload();
		return;
	}

	m_aPollFds[0].fd = m_ListenSocket;
	m_aPollFds[0].events = POLLIN;
	m_PollFds++;
}

void VoiceServer::HandleNetwork()
{
	if(m_ListenSocket == -1) {
		return;
	}

	int PollRes = poll(m_aPollFds, m_PollFds, 0);
	if(PollRes <= 0) {
		return;
	}

	// Accept new clients
	if(m_aPollFds[0].revents & POLLIN)
	{
		// Find slot
		int Client;
		for(Client = 0; Client < MAX_CLIENTS; Client++)
		{
			if(m_aClients[Client].m_Socket == -1)
				break;
		}

		// no free slot
		if(Client != MAX_CLIENTS)
		{
			sockaddr_in addr;
			size_t size = sizeof(sockaddr_in);
			int Socket = accept(m_ListenSocket, (sockaddr *)&addr, &size);

			m_aClients[Client].m_Socket = Socket;
			m_aClients[Client].m_BufferWriteIndex = 0;
			m_aClients[Client].m_LastLength = 0;
			m_aClients[Client].m_LastValidData = 0.0;
			m_aClients[Client].m_New = true;
			m_aClients[Client].m_UnEven = false;

			m_aPollFds[m_PollFds].fd = Socket;
			m_aPollFds[m_PollFds].events = POLLIN | POLLHUP;
			m_aPollFds[m_PollFds].revents = 0;
			m_PollFds++;

		#ifdef DEBUG_CONNECTION
			smutils->LogMessage(myself, "Client %d connected!\n", Client);
		#endif
		}
	}

	bool CompressPollFds = false;
	for(int PollFds = 1; PollFds < m_PollFds; PollFds++)
	{
		int Client = -1;
		for(Client = 0; Client < MAX_CLIENTS; Client++)
		{
			if(m_aClients[Client].m_Socket == m_aPollFds[PollFds].fd)
				break;
		}
		if(Client == -1)
			continue;

		CClient *pClient = &m_aClients[Client];

		// Connection shutdown prematurely ^C
		// Make sure to set SO_LINGER l_onoff = 1, l_linger = 0
		if(m_aPollFds[PollFds].revents & POLLHUP)
		{
			if (pClient->m_Socket != -1)
				close(pClient->m_Socket);

			pClient->m_Socket = -1;
			m_aPollFds[PollFds].fd = -1;
			CompressPollFds = true;
		#ifdef DEBUG_CONNECTION
			smutils->LogMessage(myself, "Client %d disconnected!(2)\n", Client);
		#endif
			continue;
		}

		// Data available?
		if(!(m_aPollFds[PollFds].revents & POLLIN))
			continue;

		size_t BytesAvailable;
		if(ioctl(pClient->m_Socket, FIONREAD, &BytesAvailable) == -1)
			continue;

		if(pClient->m_New)
		{
			pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
			pClient->m_New = false;
		}

		m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

		// Don't recv() when we can't fit data into the ringbuffer
		unsigned char aBuf[32768];
		if(min(BytesAvailable, sizeof(aBuf)) > m_Buffer.CurrentFree() * sizeof(int16_t))
			continue;

		// Edge case: previously received data is uneven and last recv'd byte has to be prepended
		int Shift = 0;
		if(pClient->m_UnEven)
		{
			Shift = 1;
			aBuf[0] = pClient->m_Remainder;
			pClient->m_UnEven = false;
		}

		ssize_t Bytes = recv(pClient->m_Socket, &aBuf[Shift], sizeof(aBuf) - Shift, 0);

		// workaround to reset the socket when needed
		int dummyData[] = { 0 };
		send(pClient->m_Socket, &dummyData, 1, 0);

		if(Bytes <= 0)
		{
			if (pClient->m_Socket != -1)
				close(pClient->m_Socket);

			pClient->m_Socket = -1;
			m_aPollFds[PollFds].fd = -1;
			CompressPollFds = true;
		#ifdef DEBUG_CONNECTION
			smutils->LogMessage(myself, "Client %d disconnected!(1)\n", Client);
		#endif
			continue;
		}

		Bytes += Shift;

		// Edge case: data received is uneven (can't be divided by two)
		// store last byte, drop it here and prepend it right before the next recv
		if(Bytes & 1)
		{
			pClient->m_UnEven = true;
			pClient->m_Remainder = aBuf[Bytes - 1];
			Bytes -= 1;
		}

		// Got data!
		OnDataReceived(pClient, (int16_t *)aBuf, Bytes / sizeof(int16_t));

		pClient->m_LastLength = m_Buffer.CurrentLength();
		pClient->m_BufferWriteIndex = m_Buffer.GetWriteIndex();
	}

	if(CompressPollFds)
	{
		for(int PollFds = 1; PollFds < m_PollFds; PollFds++)
		{
			if(m_aPollFds[PollFds].fd != -1)
				continue;

			for(int PollFds_ = PollFds; PollFds_ < 1 + MAX_CLIENTS; PollFds_++)
				m_aPollFds[PollFds_].fd = m_aPollFds[PollFds_ + 1].fd;

			PollFds--;
			m_PollFds--;
		}
	}
}

void VoiceServer::OnDataReceived(CClient *pClient, int16_t *pData, size_t Samples)
{
	// Check for empty input
	ssize_t DataStartsAt = -1;
	for(size_t i = 0; i < Samples; i++)
	{
		if(pData[i] == 0)
			continue;

		DataStartsAt = i;
		break;
	}

	// Discard empty data if last vaild data was more than a second ago.
	if(pClient->m_LastValidData + 1.0 < getTime())
	{
		// All empty
		if(DataStartsAt == -1)
			return;

		// Data starts here
		pData += DataStartsAt;
		Samples -= DataStartsAt;
	}

	if(!m_Buffer.Push(pData, Samples))
	{
		smutils->LogError(myself, "Buffer push failed!!! Samples: %u, Free: %u\n", Samples, m_Buffer.CurrentFree());
		return;
	}

	pClient->m_LastValidData = getTime();
}

void VoiceServer::HandleVoiceData()
{
	int SamplesPerFrame = m_EncoderSettings.FrameSize;
	int PacketSize = m_EncoderSettings.PacketSize;
	int FramesAvailable = m_Buffer.TotalLength() / SamplesPerFrame;
	float TimeAvailable = (float)m_Buffer.TotalLength() / (float)m_EncoderSettings.SampleRate_Hz;

	if(!FramesAvailable)
		return;

	// Before starting playback we want at least 100ms in the buffer
	if(m_AvailableTime < getTime() && TimeAvailable < 0.1)
		return;

	// let the clients have no more than 500ms
	if(m_AvailableTime > getTime() + 0.5)
		return;

	// 5 = max frames per packet
	FramesAvailable = min(FramesAvailable, 5);

	for(int Frame = 0; Frame < FramesAvailable; Frame++)
	{
		// Get data into buffer from ringbuffer.
		const std::size_t aBufferSize{sizeof(int16_t) * SamplesPerFrame};
		int16_t *aBuffer{new int16_t[SamplesPerFrame]{0}};

		size_t OldReadIdx = m_Buffer.m_ReadIndex;
		size_t OldCurLength = m_Buffer.CurrentLength();
		size_t OldTotalLength = m_Buffer.TotalLength();

		if(!m_Buffer.Pop(aBuffer, SamplesPerFrame))
		{
			printf("Buffer pop failed!!! Samples: %u, Length: %u\n", SamplesPerFrame, m_Buffer.TotalLength());
			return;
		}

		if(volume != 1.0)
		{
			for(int i = 0; i < SamplesPerFrame; i++)
			{
				int32_t element = aBuffer[i];
				element = round(element * volume);

				if(element > INT16_MAX)
					element = INT16_MAX;
				else if(element < INT16_MIN)
					element = INT16_MIN;

				aBuffer[i] = element;
			}
		}

		OnVoiceServerData->PushCell(handle);
		OnVoiceServerData->PushStringEx(reinterpret_cast<char *>(aBuffer), aBufferSize, SM_PARAM_STRING_COPY|SM_PARAM_STRING_BINARY, 0);
		OnVoiceServerData->PushCell(SamplesPerFrame);
		OnVoiceServerData->PushCell(PacketSize);
		OnVoiceServerData->Execute();

		delete[] aBuffer;

		// Check for buffer underruns
		for(int Client = 0; Client < MAX_CLIENTS; Client++)
		{
			CClient *pClient = &m_aClients[Client];
			if(pClient->m_Socket == -1 || pClient->m_New == true)
				continue;

			m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);

			if(m_Buffer.CurrentLength() > pClient->m_LastLength)
			{
				pClient->m_BufferWriteIndex = m_Buffer.GetReadIndex();
				m_Buffer.SetWriteIndex(pClient->m_BufferWriteIndex);
				pClient->m_LastLength = m_Buffer.CurrentLength();
			}
		}
	}

	if(m_AvailableTime < getTime())
		m_AvailableTime = getTime();

	m_AvailableTime += (double)FramesAvailable * m_EncoderSettings.FrameTime;
}

void VoiceServer::OnGameFrame(bool simulating)
{
	for(VoiceServer *it : servers) {
		it->HandleNetwork();
		it->HandleVoiceData();
	}
}

void VoiceServer::Unload()
{
	if(m_ListenSocket != -1)
	{
		close(m_ListenSocket);
		m_ListenSocket = -1;
	}

	for(int Client = 0; Client < MAX_CLIENTS; Client++)
	{
		if(m_aClients[Client].m_Socket != -1)
		{
			close(m_aClients[Client].m_Socket);
			m_aClients[Client].m_Socket = -1;
		}
	}
}
