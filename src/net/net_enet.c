/* net.c */

#include <assert.h>
#include <enet/enet.h>
#include <stdio.h>
#include <string.h>

#include "net.h"

#include "client.h"
#include "message.h"
#include "server.h"
#include "../audio/audio.h"
#include "../house.h"
#include "../mods/multiplayer.h"
#include "../opendune.h"
#include "../pool/house.h"

#if 0
#define NET_LOG(FORMAT,...)	\
	do { fprintf(stderr, "%s:%d " FORMAT "\n", __FUNCTION__, __LINE__, __VA_ARGS__); } while (false)
#else
#define NET_LOG(...)
#endif

enum HouseFlag g_client_houses;
enum NetHostType g_host_type;
static ENetHost *s_host;
static ENetPeer *s_peer;

int g_local_client_id;
static PeerData s_peer_data[MAX_CLIENTS];

/*--------------------------------------------------------------*/

static PeerData *
Net_NewPeerData(int peerID)
{
	for (int i = 0; i < MAX_CLIENTS; i++) {
		PeerData *data = &s_peer_data[i];

		if (data->id == 0) {
			data->id = peerID;
			return data;
		}
	}

	return NULL;
}

static PeerData *
Server_NewClient(void)
{
	static int l_peerID = 0;

	l_peerID = (l_peerID + 1) & 0xFF;

	if (l_peerID == 0)
		l_peerID = 1;

	return Net_NewPeerData(l_peerID);
}

static enum HouseType
Net_GetClientHouse(int peerID)
{
	if (peerID == 0)
		return HOUSE_INVALID;

	for (enum HouseType h = HOUSE_HARKONNEN; h < HOUSE_MAX; h++) {
		if (g_multiplayer.client[h] == peerID)
			return h;
	}

	return HOUSE_INVALID;
}

/*--------------------------------------------------------------*/

void
Net_Initialise(void)
{
	enet_initialize();
	atexit(enet_deinitialize);
}

static bool
Net_WaitForEvent(enum _ENetEventType type, enet_uint32 duration)
{
	for (int attempts = duration / 25; attempts > 0; attempts--) {
		ENetEvent event;

		Audio_PollMusic();

		if (enet_host_service(s_host, &event, 25) <= 0)
			continue;

		if (event.type == type)
			return true;
	}

	return false;
}

bool
Net_CreateServer(const char *addr, int port)
{
	if (g_host_type == HOSTTYPE_NONE && s_host == NULL && s_peer == NULL) {
		/* Currently at most MAX_HOUSE players, or 5 remote clients. */
		const int max_clients = MAX_CLIENTS - 1;

		ENetAddress address;
		enet_address_set_host(&address, addr);
		address.port = port;

		s_host = enet_host_create(&address, max_clients, 2, 0, 0);
		if (s_host == NULL)
			goto ERROR_HOST_CREATE;

		NET_LOG("%s", "Created server.");

		g_client_houses = 0;
		memset(s_peer_data, 0, sizeof(s_peer_data));
		memset(&g_multiplayer, 0, sizeof(g_multiplayer));

		g_host_type = HOSTTYPE_CLIENT_SERVER;
		PeerData *data = Server_NewClient();
		assert(data != NULL);

		g_local_client_id = data->id;

		return true;
	}

ERROR_HOST_CREATE:
	return false;
}

bool
Net_ConnectToServer(const char *hostname, int port)
{
	if (g_host_type == HOSTTYPE_NONE && s_host == NULL && s_peer == NULL) {
		ENetAddress address;
		enet_address_set_host(&address, hostname);
		address.port = port;

		s_host = enet_host_create(NULL, 1, 2, 57600/8, 14400/8);
		if (s_host == NULL)
			goto ERROR_HOST_CREATE;

		s_peer = enet_host_connect(s_host, &address, 2, 0);
		if (s_peer == NULL)
			goto ERROR_HOST_CONNECT;

		if (!Net_WaitForEvent(ENET_EVENT_TYPE_CONNECT, 1000))
			goto ERROR_TIMEOUT;

		NET_LOG("Connected to server %s:%d\n", hostname, port);

		memset(s_peer_data, 0, sizeof(s_peer_data));
		memset(&g_multiplayer, 0, sizeof(g_multiplayer));

		g_host_type = HOSTTYPE_DEDICATED_CLIENT;
		g_local_client_id = 0;
		return true;
	}

	goto ERROR_HOST_CREATE;

ERROR_TIMEOUT:
	enet_peer_reset(s_peer);
	s_peer = NULL;

ERROR_HOST_CONNECT:
	enet_host_destroy(s_host);
	s_host = NULL;

ERROR_HOST_CREATE:
	return false;
}

void
Net_Disconnect(void)
{
	if (s_host != NULL) {
		int connected_peers = s_host->connectedPeers;

		for (size_t i = 0; i < s_host->connectedPeers; i++) {
			enet_peer_disconnect(&s_host->peers[i], 0);
		}

		while (connected_peers > 0) {
			if (!Net_WaitForEvent(ENET_EVENT_TYPE_DISCONNECT, 3000))
				break;

			connected_peers--;
		}

		enet_host_destroy(s_host);
		s_host = NULL;
	}

	s_peer = NULL;
	g_host_type = HOSTTYPE_NONE;
}

void
Net_Synchronise(void)
{
}

/*--------------------------------------------------------------*/

void
Server_SendMessages(void)
{
	if (g_host_type != HOSTTYPE_DEDICATED_SERVER
	 && g_host_type != HOSTTYPE_CLIENT_SERVER)
		return;

	unsigned char *buf = g_server_broadcast_message_buf;

	Server_Send_UpdateCHOAM(&buf);
	Server_Send_UpdateLandscape(&buf);
	Server_Send_UpdateStructures(&buf);
	Server_Send_UpdateUnits(&buf);
	Server_Send_UpdateExplosions(&buf);

	unsigned char * const buf_start_client_specific = buf;

	for (enum HouseType houseID = HOUSE_HARKONNEN; houseID < HOUSE_MAX; houseID++) {
		if (g_multiplayer.client[houseID] == 0)
			continue;

		buf = buf_start_client_specific;

		Server_Send_UpdateHouse(houseID, &buf);
		Server_Send_UpdateFogOfWar(houseID, &buf);

		if (buf + g_server2client_message_len[houseID]
				< g_server_broadcast_message_buf + MAX_SERVER_BROADCAST_MESSAGE_LEN) {
			memcpy(buf, g_server2client_message_buf[houseID],
					g_server2client_message_len[houseID]);
			buf += g_server2client_message_len[houseID];
			g_server2client_message_len[houseID] = 0;
		}

		const int len = buf - g_server_broadcast_message_buf;

		ENetPacket *packet
			= enet_packet_create(g_server_broadcast_message_buf, len,
					ENET_PACKET_FLAG_RELIABLE);

		for (int i = 0; i < MAX_CLIENTS; i++) {
			const PeerData *data = &s_peer_data[i];
			ENetPeer *peer = data->peer;

			if (peer == NULL || Net_GetClientHouse(data->id) != houseID)
				continue;

			NET_LOG("packet size=%d, num outgoing packets=%lu",
					len, enet_list_size(&peer->outgoingReliableCommands));

			enet_peer_send(peer, 0, packet);
		}
	}
}

static void
Server_Recv_ConnectClient(ENetEvent *event)
{
	NET_LOG("A new client connected from %x:%u.",
			event->peer->address.host, event->peer->address.port);

	PeerData *data = Server_NewClient();
	if (data != NULL) {
		event->peer->data = data;
		data->peer = event->peer;
	}
	else {
		enet_peer_disconnect(event->peer, 0);
	}
}

static void
Server_Recv_DisconnectClient(ENetEvent *event)
{
	NET_LOG("Disconnect client from %x:%u.",
			event->peer->address.host, event->peer->address.port);

	PeerData *data = event->peer->data;

	const enum HouseType houseID = Net_GetClientHouse(data->id);
	if (houseID != HOUSE_INVALID) {
		House *h = House_Get_ByIndex(houseID);
		g_multiplayer.client[houseID] = 0;

		g_client_houses &= ~(1 << houseID);
		h->flags.human = false;
		h->flags.isAIActive = true;
	}

	data->id = 0;
	data->peer = NULL;

	enet_peer_disconnect(event->peer, 0);
}

void
Server_RecvMessages(void)
{
	if (g_host_type == HOSTTYPE_DEDICATED_CLIENT)
		return;

	/* Process the local player's commands. */
	if (g_host_type == HOSTTYPE_NONE
	 || g_host_type == HOSTTYPE_CLIENT_SERVER) {
		Server_ProcessMessage(g_playerHouseID,
				g_client2server_message_buf, g_client2server_message_len);
		g_client2server_message_len = 0;

		if (g_host_type == HOSTTYPE_NONE)
			return;
	}

	ENetEvent event;
	while (enet_host_service(s_host, &event, 0) > 0) {
		switch (event.type) {
			case ENET_EVENT_TYPE_RECEIVE:
				{
					ENetPacket *packet = event.packet;
					const PeerData *data = event.peer->data;
					const enum HouseType houseID = Net_GetClientHouse(data->id);
					Server_ProcessMessage(houseID,
							packet->data, packet->dataLength);
					enet_packet_destroy(packet);
				}
				break;

			case ENET_EVENT_TYPE_CONNECT:
				Server_Recv_ConnectClient(&event);
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				Server_Recv_DisconnectClient(&event);
				break;

			case ENET_EVENT_TYPE_NONE:
			default:
				break;
		}
	}
}

void
Client_SendMessages(void)
{
	if (g_host_type == HOSTTYPE_DEDICATED_SERVER)
		return;

	Client_Send_BuildQueue();

	if (g_host_type != HOSTTYPE_DEDICATED_CLIENT)
		return;

	if (g_client2server_message_len <= 0)
		return;

	NET_LOG("packet size=%d, num outgoing packets=%lu",
			g_client2server_message_len,
			enet_list_size(&s_host->peers[0].outgoingReliableCommands));

	ENetPacket *packet
		= enet_packet_create(
				g_client2server_message_buf, g_client2server_message_len,
				ENET_PACKET_FLAG_RELIABLE);

	enet_peer_send(s_peer, 0, packet);
	g_client2server_message_len = 0;
}

enum NetEvent
Client_RecvMessages(void)
{
	if (g_host_type == HOSTTYPE_DEDICATED_SERVER) {
		return NETEVENT_NORMAL;
	}
	else if (g_host_type != HOSTTYPE_DEDICATED_CLIENT) {
		House_Client_UpdateRadarState();
		Client_ChangeSelectionMode();
		return NETEVENT_NORMAL;
	}

	ENetEvent event;
	while (enet_host_service(s_host, &event, 0) > 0) {
		switch (event.type) {
			case ENET_EVENT_TYPE_RECEIVE:
				{
					ENetPacket *packet = event.packet;
					Client_ProcessMessage(packet->data, packet->dataLength);
					enet_packet_destroy(packet);
				}
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				Net_Disconnect();
				return NETEVENT_DISCONNECT;

			case ENET_EVENT_TYPE_CONNECT:
			case ENET_EVENT_TYPE_NONE:
			default:
				break;
		}
	}

	return NETEVENT_NORMAL;
}