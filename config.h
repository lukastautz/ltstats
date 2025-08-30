// In the agent token file the server key/id (a 32 character hexadecimal string) is saved
#define CONFIG_DEFAULT_TOKEN_PATH "/etc/monitoring_token"

#define CONFIG_MEASURE_EVERY_N_SECONDS 60

// Cache is stored in stack because it will only use a small amount of RAM (default setting of 24 * 60 max elements will use around 45 KiB RAM, which, while not being _that_ small, is still acceptable, and should be no problem on most linux systems given that the default stack limit is 8 MiB, and as allocation is not neccessary, it is way more efficient than malloc+free on heap.) Be wary with increasing this value, and be sure to check that it isn't above the stack limit, and keep in mind that large values don't really make sense usually as it will be lost when the agent is restarted.
#define CONFIG_MAX_CACHED 24 * 60 // can cache a whole day of data if CONFIG_MEASURE_EVERY_N_SECONDS is 60

#define DECLARE_DOWN_IF_N_SECONDS_WITHOUT_DATA 202 // allow two uploads (including the first retry) to fail, plus 20 seconds for each upload (min is 60, max is 255)

#define CONFIG_UPLOAD_MAX_N_STATS_AT_ONCE 64 // (max is 255)

#define SERVER_LISTEN_BACKLOG SOMAXCONN

// #define LISTEN_ALL // this is necessary for docker as otherwise it will not be reachable from outside of the container itself
