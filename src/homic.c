#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <libxal.h>

#include <homic.h>
#include <homi_proto.h>

struct homic_client {
	int fd;
	size_t xal_count;
	struct xal **xals;
};

static struct homic_client *g_homic_client = NULL;

int
homic_connect(char *socket_path)
{
	struct homic_client *cand;
	struct sockaddr_un saddr;
	int fd, err;

	cand = calloc(1, sizeof(*cand));
	if (!cand) {
		err = -errno;
		fprintf(stderr, "Failed: calloc(); err(%d)\n", err);
		return err;
	}

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0) {
		err = -errno;
		fprintf(stderr, "Failed: socket(); err(%d)\n", err);
		goto failed;
	}

	saddr.sun_family = AF_LOCAL;
	strncpy(saddr.sun_path, socket_path, sizeof(saddr.sun_path));
	saddr.sun_path[sizeof(saddr.sun_path) - 1] = '\0';

	err = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
	if (err) {
		err = -errno;
		fprintf(stderr, "Failed: connect(); err(%d)\n", err);
		close(fd);
		goto failed;
	}

	cand->fd = fd;
	g_homic_client = cand;

	return 0;

failed:
	free(cand);
	return err;
}

void
homic_disconnect()
{
	if (!g_homic_client) {
		return;
	}

	if (g_homic_client->fd >= 0) {
		close(g_homic_client->fd);
	}

	for (size_t i = 0; i < g_homic_client->xal_count; i++) {
		struct xal *xal = g_homic_client->xals[i];
		xal_close(xal);
	}
	free(g_homic_client->xals);

	free(g_homic_client);
	g_homic_client = NULL;
}

int
homic_helloworld(int32_t value, char **out)
{
	struct homi_msg_header hdr = {0};
	struct homi_req_helloworld req = {0};
	enum homi_msg_type msg_type = HOMI_MSG_TYPE_HELLOWORLD;
	void *response = NULL;
	int err;

	if (!g_homic_client) {
		err = -ENOTCONN;
		fprintf(stderr, "Failed: No connection, please call homic_client_connect(); err(%d)\n", err);
		return err;
	}

	req.value = value;
	hdr.type = msg_type;
	hdr.payload_len = sizeof(req);

	err = homi_proto_socket_write(g_homic_client->fd, &hdr, &req, sizeof(req));
	if (err) {
		fprintf(stderr, "Failed: homi_proto_socket_write(hdr); err(%d)\n", err);
		return err;
	}

	err = homi_proto_socket_read(g_homic_client->fd, &hdr, (void **)&response);
	if (err) {
		fprintf(stderr, "Failed: homi_proto_socket_read(hdr); err(%d)\n", err);
		return err;
	}

	*out = response;
	return 0;
}

int
homic_connect_xal(char *dev_uri, struct xal **out)
{
	struct homi_msg_header hdr = {0};
	struct homi_req_xal_connect req = {0};
	struct homi_res_xal_connect *res = NULL;
	struct xal **new_xal;
	char shm_name[64];
	size_t new_count;
	int err;

	if (!g_homic_client) {
		err = -ENOTCONN;
		fprintf(stderr, "Failed: No connection, please call homic_connect(); err(%d)\n", err);
		return err;
	}

	strncpy(req.dev_uri, dev_uri, sizeof(req.dev_uri) - 1);
	hdr.type = HOMI_MSG_TYPE_XAL_CONNECT;

	err = homi_proto_socket_write(g_homic_client->fd, &hdr, &req, sizeof(req));
	if (err) {
		fprintf(stderr, "Failed: homi_proto_socket_write(); err(%d)\n", err);
		return err;
	}

	err = homi_proto_socket_read(g_homic_client->fd, &hdr, (void **)&res);
	if (err) {
		fprintf(stderr, "Failed: homi_proto_socket_read(); err(%d)\n", err);
		return err;
	}
	if (res->err) {
		err = res->err;
		fprintf(stderr, "Failed: daemon xal_connect error; err(%d)\n", err);
		goto exit;
	}

	/* Copy out of shm before any further operations touch the segment. */
	memcpy(shm_name, res->shm_name, sizeof(shm_name));

	err = xal_from_shm(shm_name, out);
	if (err) {
		fprintf(stderr, "Failed: xal_from_shm(); err(%d)\n", err);
		goto exit;
	}

	new_count = g_homic_client->xal_count + 1;
	new_xal = realloc(g_homic_client->xals, new_count * sizeof(*g_homic_client->xals));
	if (!new_xal) {
		err = -ENOMEM;
		goto exit;
	}

	new_xal[new_count - 1] = *out;
	g_homic_client->xals = new_xal;
	g_homic_client->xal_count = new_count;

exit:
	free(res);

	return err;
}
