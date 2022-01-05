#include <string.h>
#include <stdio.h>
#include "ringbuffer.h"

#undef NDEBUG
#include <assert.h>

template <typename T> inline T min(T a, T b) { return a<b?a:b; }

CRingBuffer::CRingBuffer() : m_BufferSize{0}, m_aBuffer{nullptr}
{
	m_ReadIndex = 0;
	m_WriteIndex = 0;
	m_Length = 0;
}

CRingBuffer::CRingBuffer(std::size_t size) : m_BufferSize{size}, m_aBuffer{new int16_t[size]{0}}
{
	m_ReadIndex = 0;
	m_WriteIndex = 0;
	m_Length = 0;
}

void CRingBuffer::Init(std::size_t size)
{
	m_aBuffer = new int16_t[size]{0};
	m_BufferSize = size;
}

CRingBuffer::~CRingBuffer()
{
	delete[] m_aBuffer;
}

bool CRingBuffer::Pop(int16_t *pBuffer, size_t Samples)
{
	if(Samples > TotalLength())
		return false;

	if(m_ReadIndex + Samples > m_BufferSize)
	{
		size_t TowardsEnd = m_BufferSize - m_ReadIndex;
		memcpy(pBuffer, &m_aBuffer[m_ReadIndex], TowardsEnd * sizeof(*m_aBuffer));
		m_ReadIndex = 0;

		size_t Left = Samples - TowardsEnd;
		memcpy(&pBuffer[TowardsEnd], m_aBuffer, Left * sizeof(*m_aBuffer));
		m_ReadIndex = Left;
	}
	else
	{
		memcpy(pBuffer, &m_aBuffer[m_ReadIndex], Samples * sizeof(*m_aBuffer));
		m_ReadIndex += Samples;
	}

	if(m_ReadIndex == m_BufferSize)
		m_ReadIndex = 0;

	m_Length -= Samples;

	return true;
}

void CRingBuffer::Mix(int16_t *pData, size_t Samples)
{
	assert(!(m_WriteIndex + Samples > m_BufferSize));

	int16_t *pBuffer = &m_aBuffer[m_WriteIndex];
	while(Samples--)
	{
		int32_t Sample = *pBuffer;
		Sample += *pData;

		if(Sample > INT16_MAX)
			*pBuffer = INT16_MAX;
		else if(Sample < INT16_MIN)
			*pBuffer = INT16_MIN;
		else
			*pBuffer = Sample;

		pBuffer++;
		pData++;
	}
}

bool CRingBuffer::Push(int16_t *pData, size_t Samples)
{
	if(Samples > CurrentFree())
		return false;

	// Mix with data in front of us
	if(CurrentLength() < TotalLength())
	{
		//
		size_t ToMix = min(Samples, TotalLength() - CurrentLength());

		if(m_WriteIndex + ToMix > m_BufferSize)
		{
			size_t TowardsEnd = m_BufferSize - m_WriteIndex;
			Mix(pData, TowardsEnd);
			m_WriteIndex = 0;

			size_t Left = ToMix - TowardsEnd;
			Mix(&pData[TowardsEnd], Left);
			m_WriteIndex = Left;
		}
		else
		{
			Mix(pData, ToMix);
			m_WriteIndex += ToMix;
		}

		if(m_WriteIndex == m_BufferSize)
			m_WriteIndex = 0;

		pData += ToMix;
		Samples -= ToMix;
	}

	//
	if(!Samples)
		return true;

	if(m_WriteIndex + Samples > m_BufferSize)
	{
		size_t TowardsEnd = m_BufferSize - m_WriteIndex;
		memcpy(&m_aBuffer[m_WriteIndex], pData, TowardsEnd * sizeof(*m_aBuffer));
		m_WriteIndex = 0;

		size_t Left = Samples - TowardsEnd;
		memcpy(m_aBuffer, &pData[TowardsEnd], Left * sizeof(*m_aBuffer));
		m_WriteIndex = Left;
	}
	else
	{
		memcpy(&m_aBuffer[m_WriteIndex], pData, Samples * sizeof(*m_aBuffer));
		m_WriteIndex += Samples;
	}

	if(m_WriteIndex == m_BufferSize)
		m_WriteIndex = 0;

	m_Length += Samples;

	return true;
}

size_t CRingBuffer::TotalLength()
{
	return m_Length;
}

size_t CRingBuffer::TotalFree()
{
	return m_BufferSize - m_Length - 1;
}

size_t CRingBuffer::CurrentLength()
{
	return ((ssize_t)m_WriteIndex - (ssize_t)m_ReadIndex) % m_BufferSize;
}

size_t CRingBuffer::CurrentFree()
{
	size_t BufferFree = ((ssize_t)m_ReadIndex - (ssize_t)m_WriteIndex) % m_BufferSize;
	return (BufferFree ? BufferFree : m_BufferSize) - 1;
}

size_t CRingBuffer::GetReadIndex()
{
	return m_ReadIndex;
}

size_t CRingBuffer::GetWriteIndex()
{
	return m_WriteIndex;
}

void CRingBuffer::SetWriteIndex(size_t WriteIndex)
{
	m_WriteIndex = WriteIndex;
}
