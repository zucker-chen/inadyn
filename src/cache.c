/* Reads and updates cache files
 *
 * A user may have several DNS records to update.  Earlier versions of
 * inadyn supports this, but only recorded changes in one cache file.
 * This made keeping track of update times per record impossible.  Now
 * inadyn records each DNS entry to be updated in its own cache file,
 * enabling individual updates and tracking the file MTIME better.
 *
 * At startup inadyn will fall back to the old cache file and remove it
 * once it has read the IP and the modification time.
 */

#include <resolv.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ddns.h"

static int nslookup(ddns_alias_t *alias)
{
	int error;
	char address[MAX_ADDRESS_LEN];
	struct addrinfo hints;
	struct addrinfo *result;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;	/* IPv4 */
	hints.ai_socktype = SOCK_DGRAM;	/* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          /* Any protocol */

	error = getaddrinfo(alias->name, NULL, &hints, &result);
	if (!error) {
		/* DNS reply for alias found, convert to IP# */
		if (!getnameinfo(result->ai_addr, result->ai_addrlen, address, sizeof(address), NULL, 0, NI_NUMERICHOST)) {
			/* Update local record for next checkip call. */
			strncpy(alias->address, address, sizeof(alias->address));
			logit(LOG_INFO, "Resolving hostname %s => IP# %s", alias->name, address);
		}

		freeaddrinfo(result);
		return 0;
	}

	logit(LOG_WARNING, "Failed resolving hostname %s: %s", alias->name, gai_strerror(error));

	return 1;
}

static void read_one(ddns_alias_t *alias, int nonslookup)
{
	FILE *fp;
        char path[256];
	char address[MAX_ADDRESS_LEN];

        alias->last_update = 0;
        memset(alias->address, 0, sizeof(alias->address));

        snprintf(path, sizeof(path), CACHE_FILE, alias->name);
	fp = fopen(path, "r");
	if (!fp) {
                if (nonslookup)
                        return;

		/* Try a DNS lookup of our last known IP#. */
                nslookup(alias);
	} else {
		struct stat st;

		if (fgets(address, sizeof(address), fp)) {
			logit(LOG_INFO, "Cached IP# %s from previous invocation.", address);
                        strncpy(alias->address, address, sizeof(alias->address));
		}

		/* Initialize time since last update from modification time of cache file. */
		if (!fstat(fileno(fp), &st)) {
			alias->last_update = st.st_mtime;
			logit(LOG_INFO, "Last update of %s on %s", alias->name, ctime(&st.st_mtime));
		}

		fclose(fp);
	}
}

/* At boot, or when restarting inadyn at runtime, the memory struct holding our
 * current IP# is empty.  We want to avoid unnecessary updates of our DDNS server
 * record, since we might get locked out for abuse, so we "seed" each of the DDNS
 * records of our struct with the cached IP# from our cache file, or from a regular
 * DNS query. */
int read_cache_file(ddns_t *ctx)
{
	int i, j;

        /* Clear DNS cache before querying for the IP below, this to
         * prevent any artefacts from, e.g., nscd, which is a known
         * problem with DDNS clients. */
        res_init();

	if (!ctx)
		return RC_INVALID_POINTER;

        for (i = 0; i < ctx->info_count; i++) {
                ddns_info_t *info = &ctx->info[i];
                int nonslookup;

                /* Exception for tunnelbroker.net - no name to lookup */
                nonslookup = !strcmp(info->system->name, "ipv6tb@he.net");
// XXX: TODO better plugin identifiction here
                for (j = 0; j < info->alias_count; j++)
                        read_one(&info->alias[j], nonslookup);
        }
        
	return 0;
}

/* Update cache with new IP 
 *  /var/run/inadyn/my.server.name.cache { LAST-IPADDR } MTIME */
int write_cache_file(ddns_alias_t *alias)
{
	FILE *fp;
        char path[256];

        snprintf(path, sizeof(path), CACHE_FILE, alias->name);
	fp = fopen(path, "w");
	if (fp) {
		fprintf(fp, "%s", alias->address);
		fclose(fp);

		return 0;
	}

	return 1;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
