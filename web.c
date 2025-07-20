#include "server.h"

json_tokener *body_tokener;
bool body_read_cont;

json_object *parse_body_json(void) {
    uint16 body = get_http_body();
    body_read_cont = false;
    if (!body || body > len || !(body_tokener = json_tokener_new()))
        return NULL;
    http_buf[len] = '\0';
    json_object *json_body = json_tokener_parse_ex(body_tokener, http_buf + body, len - body);
    if (!json_body && json_tokener_get_error(body_tokener) == json_tokener_continue) {
        body_read_cont = true;
        return NULL;
    }
    json_tokener_free(body_tokener);
    if (!json_body)
        return NULL;
    if (!json_object_is_type(json_body, json_type_object)) {
        json_object_put(json_body);
        return NULL;
    }
    return json_body;
}

json_object *parse_additional(void) {
    http_buf[len] = '\0';
    body_read_cont = false;
    json_object *json_body = json_tokener_parse_ex(body_tokener, http_buf, len);
    if (!json_body && json_tokener_get_error(body_tokener) == json_tokener_continue) {
        body_read_cont = true;
        return NULL;
    }
    json_tokener_free(body_tokener);
    if (!json_body)
        return NULL;
    if (!json_object_is_type(json_body, json_type_object)) {
        json_object_put(json_body);
        return NULL;
    }
    return json_body;
}

int rename(const char *, const char *);


/*
Admin APIs:
  GET /admin/logged_in:
    returns HTTP 200 if logged in, any other response (or none at all) means the user isn't authenticated
  POST /admin/login:
    POST data: {"hash":"SHA256_HASH"}
    returns HTTP 200 and sets a cookie if the password is correct
    returns HTTP 401 otherwise
  GET /admin/data:
    returns the contents of data.json
  POST /admin/data:
    POST data: new content of data.json, time should stay unchanged
    returns 409 (Conflict) if the file was changed after the data that was edited was fetched, saving can be forced by setting time to 0
    returns 200 if the file was successfully saved and the server will therefore reload then
*/
#define ADMIN_STATE_LOGIN 2
void admin_process_request(uint8 state) {
    if (state == ADMIN_STATE_LOGIN) { // POST /admin/login
        json_object *body = parse_body_json(), *hash;
        while (!body && body_read_cont && sock_ready(client, false) && (len = read(client, http_buf, sizeof(http_buf) - 1)) > 0)
            body = parse_additional();
        const char *hash_str;
        if (!body || !json_object_object_get_ex(body, "hash", &hash) || !json_object_is_type(hash, json_type_string) || json_object_get_string_len(hash) != 64 || !(hash_str = json_object_get_string(hash)))
            return;
        if (memcmp(hash_str, admin_hash, 64)) { // indicate error to the client
            client_write("HTTP/1.1 401\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            return;
        }
        uint16 len = 0;
        str_append(http_buf, &len, "HTTP/1.1 200\r\nContent-Length: 0\r\nConnection: close\r\nSet-Cookie: hash=");
        str_append_len(http_buf, &len, hash_str, 64);
        str_append(http_buf, &len, "; HttpOnly; Max-Age=7889400; SameSite=Strict; Secure; Path=/\r\n\r\n");
        client_write_len(http_buf, len);
        return;
    }
    if (http_buf_compare("", "GET /admin/logged_in")) {
        __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
        client_write("HTTP/1.1 200\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    } else if (http_buf_compare("", "GET /admin/data")) {
        __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
        client_write_json(data_json);
    } else if (http_buf_compare("", "POST /admin/data")) {
        json_object *new = parse_body_json(), *hash;
        const char *hash_str;
        while (!new && body_read_cont && sock_ready(client, false) && (len = read(client, http_buf, sizeof(http_buf) - 1)) > 0)
            new = parse_additional();
        if (!new || !json_object_is_type(new, json_type_object) || !json_object_object_get_ex(new, "hash", &hash) || !json_object_is_type(hash, json_type_string) || json_object_get_string_len(hash) != 64 || !(hash_str = json_object_get_string(hash))) {
            __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
            return;
        }
        json_object *current_json_time_obj, *json_time_obj;
        uint64 current_json_time, json_time;
        if (!json_object_object_get_ex(data_json, "time", &current_json_time_obj) || !json_object_object_get_ex(new, "time", &json_time_obj) || !json_object_is_type(current_json_time_obj, json_type_int) || !json_object_is_type(json_time_obj, json_type_int) || !(current_json_time = json_object_get_uint64(current_json_time_obj))) {
            __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
            return;
        }
        json_time = json_object_get_uint64(json_time_obj);
        if (json_time && json_time != current_json_time) {
            __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
            client_write("HTTP/1.1 409\r\nContent-Length: 0\r\n\r\nConnection: close\r\n");
            return;
        }
        json_object_set_uint64(json_time_obj, time(NULL));
        size_t json_len, written = 0;
        const char *json_str = json_object_to_json_string_length(new, JSON_C_TO_STRING_PLAIN, &json_len);
        int tmp, fd = open("data.json.new", O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR);
        if (!json_str || !json_len || fd == -1) {
            __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
            close(fd);
            return;
        }
        while (written < json_len && (tmp = write(fd, json_str + written, json_len - written)) > 0)
            written += tmp;
        close(fd);
        if (written != json_len || rename("data.json.new", "data.json")) {
            __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
            return;
        }
        __atomic_store_n(admin_proc, false, __ATOMIC_RELAXED);
        __atomic_store_n(data_json_changed, true, __ATOMIC_RELAXED);
        uint16 len = 0;
        str_append(http_buf, &len, "HTTP/1.1 200\r\nContent-Length: 0\r\nConnection: close\r\n");
        if (memcmp(hash_str, admin_hash, 64)) {
            str_append(http_buf, &len, "Set-Cookie: hash=");
            str_append_len(http_buf, &len, hash_str, 64);
            str_append(http_buf, &len, "; HttpOnly; Max-Age=7889400; SameSite=Strict; Secure; Path=/\r\n");
        }
        str_append(http_buf, &len, "\r\n");
        client_write_len(http_buf, len);
    }
}

enum {
    PAGE_ERROR,
    PAGE_PERMISSION_ERROR,
    PAGE_SHOW_ALL,
    PAGE_SUCCESS
} PACKED get_page_from_buf(uint8 name_starting_from, json_object **name, json_object **monitors, bool admin) {
    char *page = http_buf + name_starting_from;
    for (uint16 i = 0; i < (uint32)len - name_starting_from; ++i) {
        if (page[i] == '\r' || page[i] == '\n')
            return PAGE_ERROR;
        if (page[i] == ' ' || page[i] == '/') {
            page[i] = '\0';
            break;
        }
    }
    if (!page[0]) {
        if (admin) {
            if (name && !(*name = json_object_new_string("All servers")))
                return PAGE_ERROR;
            return PAGE_SHOW_ALL;
        }
        page = "main";
    }
    json_object *page_object, *page_name, *page_public, *page_monitors;
    if (!json_object_object_get_ex(status_pages, page, &page_object) || !json_object_is_type(page_object, json_type_array) || json_object_array_length(page_object) != 3 ||
        !(page_name = json_object_array_get_idx(page_object, 0)) || !json_object_is_type(page_name, json_type_string) ||
        !(page_public = json_object_array_get_idx(page_object, 1)) || !json_object_is_type(page_public, json_type_boolean) ||
        !(page_monitors = json_object_array_get_idx(page_object, 2)) || !json_object_is_type(page_monitors, json_type_array))
        return PAGE_ERROR;
    if (!json_object_get_boolean(page_public) && !admin)
        return PAGE_PERMISSION_ERROR;
    if (name)
        *name = page_name;
    if (monitors)
        *monitors = page_monitors;
    return PAGE_SUCCESS;
}
#define HANDLE_HIDE(type) \
    if (!admin && !monitor->public && should_hide[SHOULD_HIDE_##type]) \
        json_object_array_add(monitor_data, minus_one); \
    else
#define ADD_DOUBLE_FROM_TWO_UINTS(to, name) json_object_array_add(to, json_object_new_double(TO_DOUBLE_FROM_TWO_UINTS(monitor->stats.name)))
json_object *get_monitor_details_json(monitor_details_t *monitor, json_object *monitor_name, uint32 now, bool admin) {
    uint32 down_seconds = now - monitor->stats.time; // handle different times, assume that the monitor is offline if there's a significant clock difference to make the user aware
    if (monitor->stats.time > now) {
        if (monitor->stats.time - now > 20) // allow minor clock differences
            down_seconds = (uint32)-1;
        else
            down_seconds = 0;
    }
    json_object *monitor_data, *minus_one = json_object_new_int(-1);
    if (!monitor ||
        !(monitor_data = json_object_new_array()))
        return NULL;
    json_object_array_add(monitor_data, monitor_name);
    if (monitor->was_online && down_seconds < DECLARE_DOWN_IF_N_SECONDS_WITHOUT_DATA) {
        HANDLE_HIDE(KERNEL) json_object_array_add(monitor_data, json_object_new_string_len(monitor->details.linux_version, monitor->details.linux_version_len));
        HANDLE_HIDE(CPU_MODEL) json_object_array_add(monitor_data, json_object_new_string_len(monitor->details.cpu_model, monitor->details.cpu_model_len));
        HANDLE_HIDE(CPU_CORES) json_object_array_add(monitor_data, json_object_new_uint64(monitor->details.cpu_cores));
        HANDLE_HIDE(UPTIME) json_object_array_add(monitor_data, json_object_new_uint64(monitor->details.uptime));
        HANDLE_HIDE(CPU_USAGE) ADD_DOUBLE_FROM_TWO_UINTS(monitor_data, cpu_usage);
        HANDLE_HIDE(CPU_IOWAIT) ADD_DOUBLE_FROM_TWO_UINTS(monitor_data, cpu_iowait);
        HANDLE_HIDE(CPU_STEAL) ADD_DOUBLE_FROM_TWO_UINTS(monitor_data, cpu_steal);
        HANDLE_HIDE(RAM_SIZE) json_object_array_add(monitor_data, json_object_new_uint64(monitor->details.ram_size));
        HANDLE_HIDE(RAM_USAGE) ADD_DOUBLE_FROM_TWO_UINTS(monitor_data, ram_usage);
        HANDLE_HIDE(SWAP_SIZE) json_object_array_add(monitor_data, json_object_new_uint64(monitor->details.swap_size));
        HANDLE_HIDE(SWAP_USAGE) ADD_DOUBLE_FROM_TWO_UINTS(monitor_data, swap_usage);
        HANDLE_HIDE(DISK_SIZE) json_object_array_add(monitor_data, json_object_new_uint64(monitor->details.disk_size));
        HANDLE_HIDE(DISK_USAGE) ADD_DOUBLE_FROM_TWO_UINTS(monitor_data, disk_usage);
        if (monitor->time_diff) {
            HANDLE_HIDE(NET) json_object_array_add(monitor_data, json_object_new_uint64(monitor->stats.rx_bytes / monitor->time_diff));
            HANDLE_HIDE(NET) json_object_array_add(monitor_data, json_object_new_uint64(monitor->stats.tx_bytes / monitor->time_diff));
            HANDLE_HIDE(IO) json_object_array_add(monitor_data, json_object_new_uint64(SECTOR_SIZE * (monitor->stats.read_sectors / monitor->time_diff)));
            HANDLE_HIDE(IO) json_object_array_add(monitor_data, json_object_new_uint64(SECTOR_SIZE * (monitor->stats.written_sectors / monitor->time_diff)));
        } else { // shouldn't (can't?) occur, but doesn't hurt to check
            HANDLE_HIDE(NET) json_object_array_add(monitor_data, json_object_new_uint64(0));
            HANDLE_HIDE(NET) json_object_array_add(monitor_data, json_object_new_uint64(0));
            HANDLE_HIDE(IO) json_object_array_add(monitor_data, json_object_new_uint64(0));
            HANDLE_HIDE(IO) json_object_array_add(monitor_data, json_object_new_uint64(0));
        }
    } else if (monitor->was_online && down_seconds != (uint32)-1)
        json_object_array_add(monitor_data, json_object_new_uint64(down_seconds));
    else
        json_object_array_add(monitor_data, NULL);
    return monitor_data;
}

#define SHOULD_SHOW(i) (admin || monitor->public || !should_hide[i])

void api_page_add_element(json_object *response_monitors, uint64 *rx, uint64 *tx, json_object *monitor_name, json_object *public_id, const char *public_id_str, uint32 now, int32 id, bool admin) {
    monitor_details_t *monitor = id == -1 ? get_monitor_details_by_public(public_id_str) : &details[id];
    json_object *monitor_data = get_monitor_details_json(monitor, monitor_name, now, admin);
    if (!monitor_data)
        return;
    json_object_array_add(monitor_data, public_id);
    json_object_array_add(response_monitors, monitor_data);
    if (SHOULD_SHOW(SHOULD_HIDE_TOTAL_TRAFFIC)) {
        *rx += monitor->rx_total;
        *tx += monitor->tx_total;
    }
}

/*
/api/page/{PAGE}
if {PAGE} == ""
    if is_logged_in()
        returns all monitors
    else
        PAGE="main"

Output json:
    - name: string
    - monitors: array of either [name: string, offline_details: uint|null, public_id: string] when the monitor is offline, with offline_details being the seconds since no data was received or null if no data was received since the start of the server, or, when the monitor is online [name: string, kernel_version: string, cpu_model: string, cpu_cores: uint, uptime: uint (in seconds), cpu_usage: double, cpu_iowait: double, cpu_steal: double, ram_size: uint (in bytes), ram_usage: double, swap_size: uint (in bytes), swap_usage: double, disk_size: uint (in bytes), disk_usage: double, rx_bytes_per_second: uint, tx_bytes_per_second: uint, disk_read_bytes_per_second: uint, disk_write_bytes_per_second: uint, public_id: string]. Any of those values except for name, offline_details and public id may be -1 if the value is hidden.
    - traffic: [rx_total_bytes: uint, tx_total_bytes: uint]
*/

void api_page(void) {
    json_object *page_name, *page_monitors, *response, *response_monitors, *traffic;
    bool admin = is_logged_in();
    uint8 state = get_page_from_buf(strlen("GET /api/page/"), &page_name, &page_monitors, admin);
    if (state == PAGE_ERROR || state == PAGE_PERMISSION_ERROR || !(response = json_object_new_object()) || !(response_monitors = json_object_new_array()) || !(traffic = json_object_new_array()) || json_object_object_add(response, "name", page_name) || json_object_object_add(response, "monitors", response_monitors) || json_object_object_add(response, "traffic", traffic))
        return;
    uint32 now = time(NULL);
    uint64 rx = 0, tx = 0;
    if (state == PAGE_SUCCESS)
        for (uint32 pos = 0, count = json_object_array_length(page_monitors); pos < count; ++pos) {
            json_object *public_id = json_object_array_get_idx(page_monitors, pos), *monitor_info, *monitor_name;
            const char *public_id_str;
            if (!json_object_is_type(public_id, json_type_string) || json_object_get_string_len(public_id) != 32 || !(public_id_str = json_object_get_string(public_id)) ||
                !json_object_object_get_ex(monitors, public_id_str, &monitor_info) ||
                !(monitor_name = json_object_array_get_idx(monitor_info, 1)) || !json_object_is_type(monitor_name, json_type_string))
                continue;
            api_page_add_element(response_monitors, &rx, &tx, monitor_name, public_id, public_id_str, now, -1, admin);
        }
    else if (state == PAGE_SHOW_ALL) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // allow ({}) in foreach
        uint32 pos = 0;
        json_object_object_foreach(monitors, public_id_str, val) {
            json_object *monitor_name, *public_id;
            if (strlen(public_id_str) != 32 || !(monitor_name = json_object_array_get_idx(val, 1)) || !(public_id = json_object_new_string_len(public_id_str, 32))) {
                ++pos;
                continue;
            }
            api_page_add_element(response_monitors, &rx, &tx, monitor_name, public_id, public_id_str, now, (int32)pos, admin);
            ++pos;
        }
    }
#pragma GCC diagnostic pop
    json_object_array_add(traffic, json_object_new_uint64(rx));
    json_object_array_add(traffic, json_object_new_uint64(tx));
    client_write_json(response);
}

/*
/api/data/{PUBLIC_ID}/{PERIOD}/{BACK}

{PERIOD} is either 3h, 6h, 12h, 3d, 7d, 14d, 28d, 3m, 6m, 1y, 2y
{BACK} is a positive (or 0) integer that indicated how many periods it should go back, i.e. when BACK=0 and PERIOD=3h, the last 3 hours will be sent, when BACK=1 and PERIOD=3h the 3 hours before that, and so forth.

DATA_ELEMENT=cpu_usage: double, cpu_iowait: double, cpu_steal: double, ram_usage: double, swap_usage: double, disk_usage: double, rx_bytes_per_second: uint, tx_bytes_per_second: uint, disk_read_bytes_per_second: uint, disk_write_bytes_per_second: uint. Any of those values may be missing if they are hidden, to figure out what values are hidden, details or hidden can be used.
Output json:
    - details: [name: string, offline_details: uint|null] when the monitor is offline, with offline_details being the seconds since no data was received or null if no data was received since the start of the server, or, when the monitor is online [name: string, kernel_version: string, cpu_model: string, cpu_cores: uint, uptime: uint (in seconds), cpu_usage: double, cpu_iowait: double, cpu_steal: double, ram_size: uint (in bytes), ram_usage: double, swap_size: uint (in bytes), swap_usage: double, disk_size: uint (in bytes), disk_usage: double, rx_bytes_per_second: uint, tx_bytes_per_second: uint, disk_read_bytes_per_second: uint, disk_write_bytes_per_second: uint]. Any of those values except for name, offline_details and public id may be -1 if the value is hidden.
    - max: [DATA_ELEMENT] (the maxima in this timespan)
    - avg: [DATA_ELEMENT] (the averages in this timespan)
    - traffic: [rx_bytes_in_timespan: uint, tx_bytes_in_timespan: uint, rx_bytes_total: uint, tx_bytes_total: uint]. This element may be missing if it's supposed to be hidden.
    - io: [disk_read_bytes_in_timespan: uint, disk_write_bytes_in_timespan: uint, disk_read_bytes_total: uint, disk_write_bytes_total: uint]. This element may be missing if it's supposed to be hidden.
    - data: object of maximum 400 (360 + 40 in case a downtime is between elements) DATA_ELEMENTs, if period is over 6h (6h: 6 * 60 => 360 datapoints), the averages over datapoints/360 will each be calculated, the key is the UNIX timestamp
    - hidden: [DATA_ELEMENT, except that the values are booleans and not doubles/uints]. Indicates what values are hidden.
    - notes: false|string Custom notes regarding that monitor.
*/

#define MAP_PERIOD(str, elements) if (!memcmp(period, str, sizeof(str))) return elements
uint32 period_to_elements(const char *period) {
    MAP_PERIOD("6h", 360);
    MAP_PERIOD("12h", 720);
    MAP_PERIOD("24h", 1440);
    MAP_PERIOD("3d", 4320);
    MAP_PERIOD("7d", 10080);
    MAP_PERIOD("14d", 20160);
    MAP_PERIOD("28d", 40320);
    MAP_PERIOD("3m", 131400);
    MAP_PERIOD("6m", 262800);
    MAP_PERIOD("1y", 525600);
    MAP_PERIOD("2y", 1051200);
    return 0;
}

#define IF_SHOULD_SHOW(i) \
    if (SHOULD_SHOW(i))

#define ADD_DATA(to, data, data_uint) \
    for (uint8 i = 0; i < 6; ++i) \
        IF_SHOULD_SHOW(i_to_id_percent[i]) json_object_array_add(to, json_object_new_double(data)); \
    for (uint8 i = 0; i < 4; ++i) \
        IF_SHOULD_SHOW(i_to_id_uint[i]) json_object_array_add(to, json_object_new_uint64(data_uint)); \

#define MAX_BACK 10000
void api_data(void) {
    const uint8 i_to_id_percent[] = { SHOULD_HIDE_CPU_USAGE, SHOULD_HIDE_CPU_IOWAIT, SHOULD_HIDE_CPU_STEAL, SHOULD_HIDE_RAM_USAGE, SHOULD_HIDE_SWAP_USAGE, SHOULD_HIDE_DISK_USAGE };
    const uint8 i_to_id_uint[] = { SHOULD_HIDE_NET, SHOULD_HIDE_NET, SHOULD_HIDE_IO, SHOULD_HIDE_IO };
    if ((uint32)len < strlen("GET /api/data//xx/0 HTTP/1.1\r\nHost:\r\n\r\n") + 32)
        return;
    char *public_id = http_buf + strlen("GET /api/data/"), *period = public_id + 33;
    for (uint8 i = 0; i < 32; ++i)
        if (!isxdigit(public_id[i]))
            return;
    public_id[32] = '\0';
    monitor_details_t *monitor = get_monitor_details_by_public(public_id);
    json_object *monitor_obj, *name, *response, *details_json, *max_json, *avg_json, *traffic_json = NULL, *io_json = NULL, *data_json, *hidden_json, *notes;
    bool admin = is_logged_in();
    if (!monitor || !json_object_object_get_ex(monitors, public_id, &monitor_obj) || !json_object_is_type(monitor_obj, json_type_array) || !(name = json_object_array_get_idx(monitor_obj, 1)) || !json_object_is_type(name, json_type_string) || !(notes = json_object_array_get_idx(monitor_obj, 3)))
        return;
    if (json_object_is_type(notes, json_type_array)) {
        json_object *string = json_object_array_get_idx(notes, 0), *public = json_object_array_get_idx(notes, 1);
        if (!json_object_is_type(string, json_type_string) || !json_object_is_type(public, json_type_boolean))
            return;
        if (json_object_get_boolean(public) || admin)
            notes = string;
        else
            notes = json_object_new_boolean(false);
    } else if (!json_object_is_type(notes, json_type_boolean) || json_object_get_boolean(notes))
        return;
    if (period[2] == '/')
        period[2] = '\0';
    else if (period[2] == 'h' || period[2] == 'd') // 12h/24h/14d/28d
        period[3] = '\0';
    else
        return;
    uint32 elements = period_to_elements(period), back, now = time(NULL);
    if (!elements || (back = (uint32)strtoul(period + (period[2] ? 4 : 3), NULL, 10)) > MAX_BACK ||
        !(response = json_object_new_object()) ||
        !(details_json = json_object_new_array()) ||
        !(max_json = json_object_new_array()) ||
        !(avg_json = json_object_new_array()) ||
        (SHOULD_SHOW(SHOULD_HIDE_TOTAL_TRAFFIC) && (!(traffic_json = json_object_new_array()) || json_object_object_add(response, "traffic", traffic_json))) ||
        (SHOULD_SHOW(SHOULD_HIDE_TOTAL_IO) && (!(io_json = json_object_new_array()) || json_object_object_add(response, "io", io_json))) ||
        !(data_json = json_object_new_object()) ||
        !(hidden_json = json_object_new_array()) ||
        json_object_object_add(response, "details", get_monitor_details_json(monitor, name, now, admin)) ||
        json_object_object_add(response, "max", max_json) ||
        json_object_object_add(response, "avg", avg_json) ||
        json_object_object_add(response, "data", data_json) ||
        json_object_object_add(response, "hidden", hidden_json) ||
        json_object_object_add(response, "notes", notes))
        return;
    uint32 file_len = fd_size(monitor->fd), total_elements = file_len / sizeof(stats_t), go_back_n_elements = elements * (back + 1), pos = 0, average_over_n_elements = elements / 360,
           count_for_avg = 0,
           count_for_datapoint_avg = 0,
           remaining_read = elements * sizeof(stats_t);
    if (go_back_n_elements < total_elements)
        pos = sizeof(stats_t) * (total_elements - go_back_n_elements);
    if (pos + remaining_read > file_len)
        remaining_read = total_elements * sizeof(stats_t) - pos;
    double max[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }, // cpu usage, iowait, steal, ram usage, swap usage, disk usage
           sum_for_avg[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
           sum_for_datapoint_avg[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    uint64 max_uint[5] = { 0, 0, 0, 0, 0 }, // rx, tx, disk_read, disk_write, time
           sum_for_avg_uint[4] = { 0, 0, 0, 0 },
           sum_for_datapoint_avg_uint[5] = { 0, 0, 0, 0, 0 },
           total_uint[4] = { 0, 0, 0, 0 },
           last_time = 0;
    uint16 count_of_datapoints = 0;
    int32 read_len;
    bool save_because_downtime = false;
    while (remaining_read && (read_len = pread(monitor->fd, http_buf, min(remaining_read, min(sizeof(stats_t) * (sizeof(http_buf) / sizeof(stats_t)), file_len - pos)), pos)) > 0) {
        uint16 count = read_len / sizeof(stats_t);
        pos += count * sizeof(stats_t);
        remaining_read -= count * sizeof(stats_t);
        for (uint16 i = 0; i < count; ++i) {
            stats_t *element = (stats_t *)http_buf + i;
            double data[6] = { TO_DOUBLE_FROM_TWO_UINTS(element->cpu_usage), TO_DOUBLE_FROM_TWO_UINTS(element->cpu_iowait), TO_DOUBLE_FROM_TWO_UINTS(element->cpu_steal), TO_DOUBLE_FROM_TWO_UINTS(element->ram_usage), TO_DOUBLE_FROM_TWO_UINTS(element->swap_usage), TO_DOUBLE_FROM_TWO_UINTS(element->disk_usage) };
            uint32 time_diff = last_time ? element->time - last_time : CONFIG_MEASURE_EVERY_N_SECONDS;
            if (time_diff > (32 * (average_over_n_elements + 3))) {
                time_diff = CONFIG_MEASURE_EVERY_N_SECONDS;
                save_because_downtime = count_for_datapoint_avg;
            } else
                save_because_downtime = false;
            if (!time_diff)
                time_diff = CONFIG_MEASURE_EVERY_N_SECONDS;
            for (uint8 i = 0; i < 6; ++i) {
                if (data[i] > max[i])
                    max[i] = data[i];
                sum_for_avg[i] += data[i];
                sum_for_datapoint_avg[i] += data[i];
            }
            total_uint[0] += element->rx_bytes, total_uint[1] += element->tx_bytes, total_uint[2] += element->read_sectors * SECTOR_SIZE, total_uint[3] += element->written_sectors * SECTOR_SIZE;
            uint64 data_uint[4] = { element->rx_bytes / time_diff, element->tx_bytes / time_diff, SECTOR_SIZE * (element->read_sectors / time_diff), SECTOR_SIZE * (element->written_sectors / time_diff) };
            for (uint8 i = 0; i < 4; ++i) {
                if (data_uint[i] > max_uint[i])
                    max_uint[i] = data_uint[i];
                sum_for_avg_uint[i] += data_uint[i];
                sum_for_datapoint_avg_uint[i] += data_uint[i];
            }
            ++count_for_avg, ++count_for_datapoint_avg;
            sum_for_datapoint_avg_uint[4] += element->time;
            last_time = element->time;
            if (count_for_datapoint_avg == average_over_n_elements || save_because_downtime) {
                json_object *data_element;
                if ((data_element = json_object_new_array())) {
                    ADD_DATA(data_element, sum_for_datapoint_avg[i] / count_for_datapoint_avg, sum_for_datapoint_avg_uint[i] / count_for_datapoint_avg);
                    char buf[16];
                    uint64 time = sum_for_datapoint_avg_uint[4] / count_for_datapoint_avg;
                    buf[itoa(time, buf)] = '\0';
                    json_object_object_add(data_json, buf, data_element);
                }
                memset(sum_for_datapoint_avg, 0, sizeof(sum_for_datapoint_avg));
                memset(sum_for_datapoint_avg_uint, 0, sizeof(sum_for_datapoint_avg_uint));
                count_for_datapoint_avg = 0;
                if (++count_of_datapoints == 400)
                    break;
            }
        }
    }
    if (count_for_datapoint_avg) { // if this is the case it didn't break before because the count was 400, so no check necessary
        json_object *data_element;
        if ((data_element = json_object_new_array())) {
            ADD_DATA(data_element, sum_for_datapoint_avg[i] / count_for_datapoint_avg, sum_for_datapoint_avg_uint[i] / count_for_datapoint_avg);
            char buf[16];
            uint64 time = sum_for_datapoint_avg_uint[4] / count_for_datapoint_avg;
            buf[itoa(time, buf)] = '\0';
            json_object_object_add(data_json, buf, data_element);
        }
    }
    ADD_DATA(max_json, max[i], max_uint[i]);
    if (count_for_avg) {
        ADD_DATA(avg_json, sum_for_avg[i] / count_for_avg, sum_for_avg_uint[i] / count_for_avg);
    } else {
        for (uint8 i = 0; i < 10; ++i)
            json_object_array_add(avg_json, json_object_new_uint64(0));
    }
    IF_SHOULD_SHOW(SHOULD_HIDE_TOTAL_TRAFFIC) {
        json_object_array_add(traffic_json, json_object_new_uint64(total_uint[0]));
        json_object_array_add(traffic_json, json_object_new_uint64(total_uint[1]));
        json_object_array_add(traffic_json, json_object_new_uint64(monitor->rx_total));
        json_object_array_add(traffic_json, json_object_new_uint64(monitor->tx_total));
    }
    IF_SHOULD_SHOW(SHOULD_HIDE_TOTAL_IO) {
        json_object_array_add(io_json, json_object_new_uint64(total_uint[2]));
        json_object_array_add(io_json, json_object_new_uint64(total_uint[3]));
        json_object_array_add(io_json, json_object_new_uint64(monitor->sectors_read_total * SECTOR_SIZE));
        json_object_array_add(io_json, json_object_new_uint64(monitor->sectors_written_total * SECTOR_SIZE));
    }
    uint8 id_to_hidden[] = {
        SHOULD_HIDE_CPU_USAGE,
        SHOULD_HIDE_CPU_IOWAIT,
        SHOULD_HIDE_CPU_STEAL,
        SHOULD_HIDE_RAM_USAGE,
        SHOULD_HIDE_SWAP_USAGE,
        SHOULD_HIDE_DISK_USAGE,
        SHOULD_HIDE_NET,
        SHOULD_HIDE_NET,
        SHOULD_HIDE_IO,
        SHOULD_HIDE_IO
    };
    for (uint8 i = 0; i < sizeof(id_to_hidden); ++i)
        json_object_array_add(hidden_json, json_object_new_boolean(!SHOULD_SHOW(id_to_hidden[i])));
    client_write_json(response);
}

void api(void) {
    if (http_buf_compare("GET /api/", "page/"))
        api_page();
    else if (http_buf_compare("GET /api/", "data/"))
        api_data();
}

void process_request(void) {
    if ((uint32)len < strlen("GET / HTTP/1.1\r\n\r\n"))
        return;
    if (http_buf_compare("GET /", "api/")) {
        api();
        return;
    }
    if (http_buf_compare("GET /", "monitor/")) {
        if ((uint32)len < strlen("GET /monitor/ HTTP/1.1\r\n\r\n") + 32 || (http_buf[strlen("GET /monitor/") + 32] != ' ' && http_buf[strlen("GET /monitor/") + 32] != '/'))
            goto not_found;
        char *public_id = http_buf + strlen("GET /monitor/");
        for (uint8 i = 0; i < 32; ++i)
            if (!isxdigit(public_id[i]))
                goto not_found;
        if (get_monitor_details_by_public(public_id)) {
            client_http_sendfile(monitor_page_fd);
            return;
        }
        goto not_found;
    }
    if (http_buf_compare("GET /", "admin")) {
        client_http_sendfile(admin_page_fd);
        return;
    }
    if (http_buf_compare("GET /", "favicon.ico")) {
        if (favicon_ico_fd == -1)
            goto not_found;
        client_http_sendfile(favicon_ico_fd);
        return;
    }
    uint8 state = get_page_from_buf(strlen("GET /"), NULL, NULL, is_logged_in());
    if (state == PAGE_ERROR)
        goto not_found;
    if (state == PAGE_PERMISSION_ERROR) {
        client_write("HTTP/1.1 302\r\nLocation: /admin\r\n\r\n");
        return;
    }
    client_http_sendfile(status_page_fd);
    return;
not_found:
    client_write("HTTP/1.1 404\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><center><h1>404 Not Found</h1></center></body></html>");
}