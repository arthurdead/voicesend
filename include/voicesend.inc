#if defined __voicesend_inc
 #endinput
#endif
#define __voicesend_inc

#define VOICESEND_NOSENDER -500

methodmap VoiceServer < Handle
{
	public native VoiceServer(const char[] addr, int port);

	public native void SetSettings(int samplerate, int framesize, int packetsize);

	property float Volume
	{
		public native get();
		public native set(float value);
	}
}

methodmap VoiceCodec < Handle
{
	public native bool Init(int quality);

	public native int Compress(const char[] pUncompressed, int nSamples, char[] pCompressed, int maxCompressedBytes, bool bFinal);
	public native int Decompress(const char[] pCompressed, int compressedBytes, char[] pUncompressed, int maxUncompressedBytes);

	public native bool ResetState();
}

native VoiceCodec CreateVoiceCodec(const char[] name);
native VoiceCodec CreateVoiceCodecEx(const char[] name, char[] error, int len);

stock VoiceCodec CreateCeltCodec(int quality = -1)
{
	VoiceCodec codec = CreateVoiceCodec("voicesend_celt");
	codec.Init(quality);
	return codec;
}

native VoiceCodec CreateCeltCodecEx(int samplerate, int framesize, int packetsize);

forward void OnVoiceServerData(VoiceServer server, const char[] uncompressed, int SamplesPerFrame, int PacketSize);

forward void OnVoiceInit(char[] codec, int length, int &samplerate);
forward void OnVoiceData(int &client, char[] data, int &length);

native void SendVoiceInit(int client, const char[] codec, int samplerate);

stock void SendVoiceDeinit(int client)
{
	SendVoiceInit(client, "", 0);
}

stock void SendVoiceReinit(int client, const char[] codec, int samplerate)
{
	SendVoiceDeinit(client);
	SendVoiceInit(client, codec, samplerate);
}

native void SendVoiceData(int client, const char[] data, int length, int from=VOICESEND_NOSENDER, bool proximity=false);

#if !defined REQUIRE_EXTENSIONS
public void __ext_voicesend_SetNTVOptional()
{
	MarkNativeAsOptional("CreateVoiceCodec");
	MarkNativeAsOptional("CreateVoiceCodecEx");
	MarkNativeAsOptional("SendVoiceInit");
	MarkNativeAsOptional("SendVoiceData");
}
#endif

public Extension __ext_voicesend =
{
	name = "voicesend",
	file = "voicesend.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};
