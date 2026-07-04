#include "precompiled.h"

const char *CRevoicePlayer::m_szCodecType[] = {
	"none",
	"silk",
	"opus",
	"speex"
};

CRevoicePlayer g_Players[MAX_PLAYERS];

CRevoicePlayer::CRevoicePlayer()
{
	m_CodecType = vct_none;
	m_SpeexCodec = new VoiceCodec_Frame(new VoiceEncoder_Speex());
	m_SilkCodec  = new CSteamP2PCodec(new VoiceEncoder_Silk());
	m_OpusCodec  = new CSteamP2PCodec(new VoiceEncoder_Opus());

	m_SpeexCodec->Init(SPEEX_VOICE_QUALITY);
	m_SilkCodec ->Init(SILK_VOICE_QUALITY);
	m_OpusCodec ->Init(OPUS_VOICE_QUALITY);

	m_Protocol = 0;
	m_HLTV = false;
	m_Connected = false;
	m_Client = nullptr;
}

void CRevoicePlayer::Initialize(IGameClient *cl)
{
	m_Client = cl;

	m_SpeexCodec->SetClient(cl);
	m_SilkCodec ->SetClient(cl);
	m_OpusCodec ->SetClient(cl);
}

void CRevoicePlayer::OnConnected()
{
	// already connected, suppose now there is a change of level?
	if (m_Connected) {
		m_VoiceFlood.Reset();
		return;
	}

	int protocol = g_ReunionApi->GetClientProtocol(m_Client->GetId());
	if (protocol != 47 && protocol != 48) {
		return;
	}

	// reset codec state
	m_SilkCodec->ResetState();
	m_OpusCodec->ResetState();
	m_SpeexCodec->ResetState();

	// default codec
	m_CodecType = GetCodecTypeByString(g_pcv_rev_default_codec->string);
	m_VoiceFlood.Reset();
	m_Connected = true;
	m_RequestId = MAKE_REQUESTID(PLID);
	m_Protocol = protocol;

	if (g_ReunionApi->GetClientAuthtype(m_Client->GetId()) == DP_AUTH_HLTV) {
		m_CodecType = GetCodecTypeByString(g_pcv_rev_hltv_codec->string);
		m_HLTV = true;
	}
	else if (m_Protocol == 48) {
		g_engfuncs.pfnQueryClientCvarValue2(m_Client->GetEdict(), "sv_version", m_RequestId);
	}
}

void CRevoicePlayer::OnDisconnected()
{
	m_HLTV = false;
	m_Connected = false;
	m_Protocol = 0;
	m_CodecType = vct_none;
	m_VoiceFlood.Reset();
	m_RequestId = 0;
}

void CRevoicePlayer::Update()
{
	if (m_HLTV) {
		m_CodecType = GetCodecTypeByString(g_pcv_rev_hltv_codec->string);
		return;
	}

	m_CodecType = GetCodecTypeByString(g_pcv_rev_default_codec->string);
	m_RequestId = MAKE_REQUESTID(PLID);

	if (m_Protocol == 48) {
		g_engfuncs.pfnQueryClientCvarValue2(m_Client->GetEdict(), "sv_version", m_RequestId);
	}
}

void Revoice_Init_Players()
{
	int maxclients = g_RehldsSvs->GetMaxClients();
	for (int i = 0; i < maxclients; i++) {
		g_Players[i].Initialize(g_RehldsSvs->GetClient(i));
	}
}

void Revoice_Update_Players(const char *pszNewValue)
{
	for (int i = 0; i < g_RehldsSvs->GetMaxClients(); i++) {
		auto plr = &g_Players[i];
		if (plr->IsConnected()) {
			plr->Update();
		}
	}
}

void Revoice_Update_Hltv(const char *pszNewValue)
{
	int maxclients = g_RehldsSvs->GetMaxClients();
	for (int i = 0; i < g_RehldsSvs->GetMaxClients(); i++) {
		auto plr = &g_Players[i];
		if (plr->IsConnected() && plr->IsHLTV()) {
			plr->Update();
		}
	}
}

CRevoicePlayer *GetPlayerByClientPtr(IGameClient *cl)
{
	return &g_Players[ cl->GetId() ];
}

CRevoicePlayer *GetPlayerByEdict(const edict_t *ed)
{
	int clientId = g_engfuncs.pfnIndexOfEdict(ed) - 1;

	if (clientId < 0 || clientId >= g_RehldsSvs->GetMaxClients()) {
		util_syserror("Invalid player edict id=%d\n", clientId);
	}

	return &g_Players[ clientId ];
}

bool CRevoicePlayer::IsVoiceFlood(double now)
{
	double maxDeltaMs = g_pcv_rev_voicemaxdelta ? g_pcv_rev_voicemaxdelta->value : 0.0;
	return m_VoiceFlood.IsFlood(now, maxDeltaMs);
}

void CRevoicePlayer::AdvanceVoiceClock(double now, int numSamples)
{
	m_VoiceFlood.Advance(now, numSamples);
}

int CRevoicePlayer::GetVoiceFloodLeadMs(double now) const
{
	return m_VoiceFlood.LeadMs(now);
}

const char *CRevoicePlayer::GetCodecTypeToString()
{
	return m_szCodecType[ m_CodecType ];
}

CodecType CRevoicePlayer::GetCodecTypeByString(const char *codec)
{
#define REV_CODEC(know_codec)\
	if (_stricmp(codec, #know_codec) == 0) {\
		return vct_##know_codec;\
	}\

	REV_CODEC(opus);
	REV_CODEC(silk);
	REV_CODEC(speex);
#undef REV_CODEC

	return vct_none;
}
