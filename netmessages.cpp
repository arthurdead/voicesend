#include "netmessages.h"
#include <tier1/convar.h>

#define NETMSG_TYPE_BITS	6	// must be 2^NETMSG_TYPE_BITS > SVC_LASTMSG
#define Bits2Bytes(b) ((b+7)>>3)

extern ConVar *sv_use_steam_voice;

static char s_text[1024];

bool SVC_VoiceInit::WriteToBuffer( bf_write &buffer )
{
	buffer.WriteUBitLong( GetType(), NETMSG_TYPE_BITS );
	buffer.WriteString( m_szVoiceCodec );
	buffer.WriteByte( /* Legacy Quality Field */ 255 );
	buffer.WriteShort( m_nSampleRate );
	return !buffer.IsOverflowed();
}

bool SVC_VoiceInit::ReadFromBuffer( bf_read &buffer )
{
	//VPROF( "SVC_VoiceInit::ReadFromBuffer" );

	buffer.ReadString( m_szVoiceCodec, sizeof(m_szVoiceCodec) );
	unsigned char nLegacyQuality = buffer.ReadByte();
	if ( nLegacyQuality == 255 )
	{
		// v2 packet
		m_nSampleRate = buffer.ReadShort();
	}
	else
	{
		// v1 packet
		//
		// Hacky workaround for v1 packets not actually indicating if we were using steam voice -- we've kept the steam
		// voice separate convar that was in use at the time as replicated&hidden, and if whatever network stream we're
		// interpreting sets it, lie about the subsequent voice init's codec & sample rate.
		if ( sv_use_steam_voice->GetBool() )
		{
			Msg( "Legacy SVC_VoiceInit - got a set for sv_use_steam_voice convar, assuming Steam voice\n" );
			V_strncpy( m_szVoiceCodec, "steam", sizeof( m_szVoiceCodec ) );
			// Legacy steam voice can always be parsed as auto sample rate.
			m_nSampleRate = 0;
		}
		else if ( V_strncasecmp( m_szVoiceCodec, "vaudio_celt", sizeof( m_szVoiceCodec ) ) == 0 )
		{
			// Legacy rate vaudio_celt always selected during v1 packet era
			m_nSampleRate = 22050;
		}
		else
		{
			// Legacy rate everything but CELT always selected during v1 packet era
			m_nSampleRate = 11025;
		}
	}

	return !buffer.IsOverflowed();
}

const char *SVC_VoiceInit::ToString(void) const
{
	Q_snprintf( s_text, sizeof(s_text), "%s: codec \"%s\", sample rate %i", GetName(), m_szVoiceCodec, m_nSampleRate );
	return s_text;
}

bool SVC_VoiceData::WriteToBuffer( bf_write &buffer )
{
	buffer.WriteUBitLong( GetType(), NETMSG_TYPE_BITS );
	buffer.WriteByte( m_nFromClient );
	buffer.WriteByte( m_bProximity );
	buffer.WriteWord( m_nLength );

	if ( IsX360() )
	{
		buffer.WriteLongLong( m_xuid );
	}

	return buffer.WriteBits( m_DataOut, m_nLength );
}

bool SVC_VoiceData::ReadFromBuffer( bf_read &buffer )
{
	//VPROF( "SVC_VoiceData::ReadFromBuffer" );

	m_nFromClient = buffer.ReadByte();
	m_bProximity = !!buffer.ReadByte();
	m_nLength = buffer.ReadWord();

	if ( IsX360() )
	{
		m_xuid =  buffer.ReadLongLong();
	}

	m_DataIn = buffer;
	return buffer.SeekRelative( m_nLength );
}

const char *SVC_VoiceData::ToString(void) const
{
	Q_snprintf(s_text, sizeof(s_text), "%s: client %i, bytes %i", GetName(), m_nFromClient, Bits2Bytes(m_nLength) );
	return s_text;
}
