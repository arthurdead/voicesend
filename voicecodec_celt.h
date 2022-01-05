#pragma once

#include "ivoicecodec.h"
#include "celt_header.h"

class VoiceCodec_Celt : public IVoiceCodec
{
public:
	VoiceCodec_Celt();
	~VoiceCodec_Celt();

	virtual bool	Init( int quality ) override;
	bool Init(celt_int32 SampleRate_Hz, celt_int32 FrameSize, celt_int32 PacketSize);

	// Use this to delete the object.
	virtual void	Release() override;


	// Compress the voice data.
	// pUncompressed		-	16-bit signed mono voice data.
	// maxCompressedBytes	-	The length of the pCompressed buffer. Don't exceed this.
	// bFinal        		-	Set to true on the last call to Compress (the user stopped talking).
	//							Some codecs like big block sizes and will hang onto data you give them in Compress calls.
	//							When you call with bFinal, the codec will give you compressed data no matter what.
	// Return the number of bytes you filled into pCompressed.
	virtual int		Compress(const char *pUncompressed, int nSamples, char *pCompressed, int maxCompressedBytes, bool bFinal) override;

	// Decompress voice data. pUncompressed is 16-bit signed mono.
	virtual int		Decompress(const char *pCompressed, int compressedBytes, char *pUncompressed, int maxUncompressedBytes) override;

	// Some codecs maintain state between Compress and Decompress calls. This should clear that state.
	virtual bool	ResetState() override;

	struct CEncoderSettings
	{
		celt_int32 SampleRate_Hz;
		celt_int32 TargetBitRate_Kbps;
		celt_int32 FrameSize;
		celt_int32 PacketSize;
		celt_int32 Complexity;
		double FrameTime;
	};

	static void InitGlobalSettings();

	static const CEncoderSettings &TheEncoderSettings();

	int	Compress(celt_int16 *pUncompressed, int nSamples, unsigned char *pCompressed, int maxCompressedBytes);

private:
	CELTMode *m_pMode;
	CELTEncoder *m_pCodec;
	CEncoderSettings m_EncoderSettings;
};
