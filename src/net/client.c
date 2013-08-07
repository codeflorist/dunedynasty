/* client.c */

#include <assert.h>
#include <stdio.h>

#include "client.h"

#include "message.h"
#include "../object.h"

#if 0
#define CLIENT_LOG(FORMAT,...)	\
	do { fprintf(stderr, "%s:%d " FORMAT "\n", __FUNCTION__, __LINE__, __VA_ARGS__); } while (false)
#else
#define CLIENT_LOG(...)
#endif

/*--------------------------------------------------------------*/

static unsigned char *
Client_GetBuffer(enum ClientServerMsg msg)
{
	assert(msg < CSMSG_MAX);

	int len = 1 + Net_GetLength_ClientServerMsg(msg);
	if (g_client2server_message_len + len >= MAX_CLIENT_MESSAGE_LEN)
		return NULL;

	unsigned char *buf = g_client2server_message_buf + g_client2server_message_len;
	g_client2server_message_len += len;

	Net_Encode_ClientServerMsg(&buf, msg);
	return buf;
}

static void
Client_Send_ObjectIndex(enum ClientServerMsg msg, const Object *o)
{
	unsigned char *buf = Client_GetBuffer(msg);
	if (buf == NULL)
		return;

	Net_Encode_ObjectIndex(&buf, o);
}
