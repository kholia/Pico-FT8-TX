///////////////////////////////////////////////////////////////////////////////
//
//  WiFi Setup Module Implementation
//
//  Handles AP mode configuration for initial WiFi setup
//
///////////////////////////////////////////////////////////////////////////////

#include "wifi_setup.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "lwip/apps/httpd.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "dhcpserver/dhcpserver.h"
#include "dnsserver/dnsserver.h"
#include "lwip/apps/fs.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// AP mode configuration
#define AP_SSID "Pico-FT8-Setup"
#define AP_PASSWORD "password"
#define AP_CHANNEL 6
#define AP_IP_ADDR "192.168.1.1"
#define AP_IP_MASK "255.255.255.0"

// Global context
static wifi_setup_context_t *g_ctx = NULL;

// WiFi scanning callback
static int scan_result_callback(void *env, const cyw43_ev_scan_result_t *result) {
    wifi_setup_context_t *ctx = (wifi_setup_context_t *)env;

    if (ctx->network_count >= WIFI_MAX_NETWORKS) {
        return 0; // Stop scanning if we have enough networks
    }

    // Copy network info
    ctx->networks[ctx->network_count].auth_mode = result->auth_mode;
    ctx->networks[ctx->network_count].rssi = result->rssi;
    strncpy(ctx->networks[ctx->network_count].ssid, result->ssid, sizeof(ctx->networks[0].ssid) - 1);
    ctx->networks[ctx->network_count].ssid[sizeof(ctx->networks[0].ssid) - 1] = '\0';

    ctx->network_count++;
    return 0; // Continue scanning
}

// HTTP server implementation based on pico-examples access_point

// Simple URL decode function
static void url_decode(char *dst, const char *src) {
    char *p = dst;
    while (*src) {
        if (*src == '+') {
            *p++ = ' ';
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            *p++ = (char)strtol(hex, NULL, 16);
            src += 2;
        } else {
            *p++ = *src;
        }
        src++;
    }
    *p = '\0';
}
#define TCP_PORT 80
#define DEBUG_printf printf
#define POLL_TIME_S 5
#define HTTP_RESPONSE_HEADERS "HTTP/1.1 %d OK\nContent-Length: %d\nContent-Type: text/html; charset=utf-8\nConnection: close\n\n"

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

typedef struct TCP_CONNECT_STATE_T_ {
    struct tcp_pcb *pcb;
    int sent_len;
    char headers[128];
    char result[2048]; // Increased buffer size for our WiFi setup page
    int header_len;
    int result_len;
    ip_addr_t *gw;
} TCP_CONNECT_STATE_T;

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err) {
    if (client_pcb) {
        assert(con_state && con_state->pcb == client_pcb);
        tcp_arg(client_pcb, NULL);
        tcp_poll(client_pcb, NULL, 0);
        tcp_sent(client_pcb, NULL);
        tcp_recv(client_pcb, NULL);
        tcp_err(client_pcb, NULL);
        tcp_close(client_pcb);
    }
    free(con_state);
    return close_err;
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    DEBUG_printf("tcp_server_sent %u\n", len);
    con_state->sent_len += len;
    if (con_state->sent_len >= con_state->header_len + con_state->result_len) {
        DEBUG_printf("all data sent\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    return ERR_OK;
}

static int test_printf(void *arg, const char *fmt, ...) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    va_list myargs;
    va_start(myargs, fmt);
    con_state->result_len += vsnprintf(&con_state->result[con_state->result_len],
                                       sizeof(con_state->result) - con_state->result_len, fmt, myargs);
    va_end(myargs);
    return 0;
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (err != ERR_OK) {
        DEBUG_printf("tcp_server_recv error %d\n", err);
        return tcp_close_client_connection(con_state, pcb, err);
    }

    if (p == NULL) {
        DEBUG_printf("connection closed\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }

    // Parse HTTP request
    char *request = (char *)p->payload;
    DEBUG_printf("tcp_server_recv: %s\n", request);

    // Check if this is a WiFi config request
    if (strstr(request, "GET /wifi_config")) {
        printf("[DEBUG] WiFi config request detected!\n");

        // Extract just the query string from the HTTP request line
        char *query_start = strchr(request, '?');
        char *query_end = NULL;
        const char *ssid = NULL;
        const char *password = NULL;
        const char *callsign = NULL;
        const char *locator = NULL;
        uint32_t frequency = 28075500UL;  // Default to 40m band

        if (query_start) {
            // Find the end of the query string (space or end of line)
            query_end = strchr(query_start, ' ');  // Space after query string in GET line
            if (!query_end) {
                query_end = strchr(query_start, '\r');  // CR
                if (!query_end) {
                    query_end = strchr(query_start, '\n');  // LF
                }
            }

            if (query_end) {
                // Temporarily null-terminate the query string
                *query_end = '\0';

                char decoded_ssid[64] = {0};
                char decoded_password[64] = {0};
                char decoded_callsign[12] = {0};
                char decoded_locator[7] = {0};
                char *query = query_start + 1;  // Skip the '?'
                char *amp_pos;

                // Parse each parameter
                while ((amp_pos = strchr(query, '&')) != NULL) {
                    *amp_pos = '\0'; // Temporarily null-terminate this parameter

                    if (strncmp(query, "ssid=", 5) == 0) {
                        url_decode(decoded_ssid, query + 5);
                        ssid = decoded_ssid;
                    } else if (strncmp(query, "password=", 9) == 0) {
                        url_decode(decoded_password, query + 9);
                        password = decoded_password;
                    } else if (strncmp(query, "callsign=", 9) == 0) {
                        url_decode(decoded_callsign, query + 9);
                        callsign = decoded_callsign;
                    } else if (strncmp(query, "locator=", 8) == 0) {
                        url_decode(decoded_locator, query + 8);
                        locator = decoded_locator;
                    } else if (strncmp(query, "frequency=", 10) == 0) {
                        frequency = strtoul(query + 10, NULL, 10);
                    }

                    query = amp_pos + 1; // Move to next parameter
                }

                // Handle last parameter (no & after it)
                if (query && *query) {
                    if (strncmp(query, "ssid=", 5) == 0) {
                        url_decode(decoded_ssid, query + 5);
                        ssid = decoded_ssid;
                    } else if (strncmp(query, "password=", 9) == 0) {
                        url_decode(decoded_password, query + 9);
                        password = decoded_password;
                    } else if (strncmp(query, "callsign=", 9) == 0) {
                        url_decode(decoded_callsign, query + 9);
                        callsign = decoded_callsign;
                    } else if (strncmp(query, "locator=", 8) == 0) {
                        url_decode(decoded_locator, query + 8);
                        locator = decoded_locator;
                    } else if (strncmp(query, "frequency=", 10) == 0) {
                        frequency = strtoul(query + 10, NULL, 10);
                    }
                }

                // Restore the null terminator we overwrote
                *query_end = ' ';
            }
        }

        // Process WiFi and station configuration
        if (ssid && password && callsign && locator &&
            strlen(ssid) > 0 && strlen(password) > 0 &&
            strlen(callsign) > 0 && strlen(locator) > 0) {
            printf("[+] Web interface: Saving config - SSID: '%s', Callsign: '%s', Locator: '%s'\n",
                   ssid, callsign, locator);

            // Save configuration
            if (wifi_setup_save_config(g_ctx, ssid, password, callsign, locator, frequency)) {
                printf("[+] Configuration saved successfully!\n");
                printf("[+] Setup complete. Device will reboot.\n");

                // Mark setup as complete
                g_ctx->ap_mode_active = false;

                // Generate success page
                con_state->result_len = 0;
                test_printf(con_state,
                    "<!DOCTYPE html>"
                    "<html>"
                    "<head><title>Setup Complete</title></head>"
                    "<body>"
                    "<h1>✅ Setup Complete!</h1>"
                    "<p>WiFi network: <strong>%s</strong></p>"
                    "<p>Callsign: <strong>%s</strong></p>"
                    "<p>Locator: <strong>%s</strong></p>"
                    "<p>Device will reboot and start FT8 transmission.</p>"
                    "<p>Please reconnect to your regular WiFi network.</p>"
                    "</body>"
                    "</html>", ssid, callsign, locator
                );
            } else {
                printf("[!] Failed to save configuration\n");
                // Generate error page
                con_state->result_len = 0;
                test_printf(con_state,
                    "<!DOCTYPE html>"
                    "<html>"
                    "<head><title>Setup Failed</title></head>"
                    "<body>"
                    "<h1>❌ Setup Failed</h1>"
                    "<p>Failed to save WiFi credentials. Please try again.</p>"
                    "<p><a href='/'>Back to Setup</a></p>"
                    "</body>"
                    "</html>"
                );
            }
        } else {
            printf("[!] Invalid parameters - ssid or password missing/empty\n");
            // Invalid parameters
            con_state->result_len = 0;
            test_printf(con_state,
                "<!DOCTYPE html>"
                "<html>"
                "<head><title>Invalid Input</title></head>"
                "<body>"
                "<h1>❌ Invalid Input</h1>"
                "<p>Please provide SSID, password, callsign, and locator.</p>"
                "<p><a href='/'>Back to Setup</a></p>"
                "</body>"
                "</html>"
            );
        }
    } else {
        // Serve the main WiFi setup page
        con_state->result_len = 0;
        test_printf(con_state,
            "<!DOCTYPE html>"
            "<html>"
            "<head><title>Pico FT8 WiFi Setup</title></head>"
            "<body>"
            "<h1>🎯 Pico FT8 Station Setup</h1>"
            "<p>Configure your WiFi network and station information:</p>"
            "<form action='/wifi_config' method='GET'>"
            "<h3>WiFi Network:</h3>"
            "<input type='text' name='ssid' placeholder='WiFi Network Name (SSID)' required autofocus><br>"
            "<input type='password' name='password' placeholder='WiFi Password' required><br>"
            "<h3>Station Information:</h3>"
            "<input type='text' name='callsign' placeholder='Your Callsign (e.g., VU3CER)' required><br>"
            "<input type='text' name='locator' placeholder='Your Locator (e.g., MK68)' required><br>"
            "<h3>FT8 Frequency Band:</h3>"
            "<select name='frequency' required>"
            "<option value='1840000'>160m (1.840 MHz)</option>"
            "<option value='3573000'>80m (3.573 MHz)</option>"
            "<option value='5357000'>60m (5.357 MHz)</option>"
            "<option value='7074000'>40m (7.074 MHz)</option>"
            "<option value='10136000'>30m (10.136 MHz)</option>"
            "<option value='14074000'>20m (14.074 MHz)</option>"
            "<option value='18100000'>17m (18.100 MHz)</option>"
            "<option value='21074000'>15m (21.074 MHz)</option>"
            "<option value='24915000'>12m (24.915 MHz)</option>"
            "<option value='28074000' selected>10m (28.074 MHz)</option>"
            "<option value='50313000'>6m (50.313 MHz)</option>"
            "<option value='70100000'>4m (70.100 MHz)</option>"
            "<option value='144174000'>2m (144.174 MHz)</option>"
            "</select><br>"
            "<input type='submit' value='Save Configuration & Start FT8'>"
            "</form>"
            "</body>"
            "</html>"
        );
    }

    // Generate HTTP headers
    con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers),
        HTTP_RESPONSE_HEADERS, 200, con_state->result_len);

    // Send headers first
    tcp_write(pcb, con_state->headers, con_state->header_len, TCP_WRITE_FLAG_COPY);
    // Then send body
    tcp_write(pcb, con_state->result, con_state->result_len, TCP_WRITE_FLAG_COPY);

    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    DEBUG_printf("tcp_server_poll_fn\n");
    return tcp_close_client_connection(con_state, pcb, ERR_OK);
}

static void tcp_server_err(void *arg, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
        tcp_close_client_connection(con_state, con_state->pcb, err);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("failure in accept\n");
        return ERR_VAL;
    }
    DEBUG_printf("client connected\n");

    // Create the state for the connection
    TCP_CONNECT_STATE_T *con_state = calloc(1, sizeof(TCP_CONNECT_STATE_T));
    if (!con_state) {
        DEBUG_printf("failed to allocate connect state\n");
        return ERR_MEM;
    }
    con_state->pcb = client_pcb;
    con_state->gw = &state->gw;

    // setup connection to client
    tcp_arg(client_pcb, con_state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(TCP_SERVER_T *state) {
    DEBUG_printf("starting server on port %d\n", TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %d\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}

bool wifi_setup_start_ap(wifi_setup_context_t *ctx) {
    printf("[+] Starting AP mode for WiFi setup\n");

    // Initialize WiFi if not already done
    printf("[+] Initializing WiFi hardware...\n");
    if (cyw43_arch_init()) {
        printf("[!] Failed to initialize WiFi hardware\n");
        return false;
    }
    printf("[+] WiFi hardware initialized\n");

    // Small delay for stability
    sleep_ms(100);

    // Enable AP mode
    printf("[+] Enabling AP mode with SSID: %s\n", AP_SSID);
    cyw43_arch_enable_ap_mode(AP_SSID, AP_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    // Set custom IP address for AP
    ip4_addr_t ip, mask, gw;
    ip4addr_aton(AP_IP_ADDR, &ip);
    ip4addr_aton(AP_IP_MASK, &mask);
    ip4_addr_set_zero(&gw);  // Gateway not used in this setup

    // Apply the IP configuration
    netif_set_addr(netif_default, &ip, &mask, &gw);
    printf("[+] AP IP address set to: %s\n", AP_IP_ADDR);

    // Start the DHCP server
    static dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &ip, &mask);
    printf("[+] DHCP server started\n");

    // Start the DNS server
    static dns_server_t dns_server;
    dns_server_init(&dns_server, &ip);
    printf("[+] DNS server started\n");

    // Set AP channel
    cyw43_wifi_ap_set_channel(&cyw43_state, AP_CHANNEL);

    printf("[+] ==========================================\n");
    printf("[+]   Pico FT8 WiFi Setup Mode Active!\n");
    printf("[+] ==========================================\n");
    printf("[+] AP Network: %s\n", AP_SSID);
    printf("[+] Password:   %s\n", AP_PASSWORD);
    printf("[+] IP Address: %s\n", AP_IP_ADDR);
    printf("[+] \n");
    printf("[+] Web Interface: http://%s\n", AP_IP_ADDR);
    printf("[+] Or use serial commands:\n");
    printf("[+] Type 'help' for available commands\n");
    printf("[+] ==========================================\n");

    // Initialize HTTP server using proven access_point implementation
    printf("[+] Starting HTTP server on port 80...\n");
    static TCP_SERVER_T tcp_server;
    memset(&tcp_server, 0, sizeof(TCP_SERVER_T));
    tcp_server.gw = ip; // Use our AP IP address

    if (!tcp_server_open(&tcp_server)) {
        printf("[-] Failed to start HTTP server\n");
        return false;
    }
    printf("[+] HTTP server listening on port 80\n");

    // httpd_post_begin callback - intercepts all HTTP requests (GET and POST)
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                      u16_t http_request_len, int content_len, char *response_uri,
                      u16_t response_uri_len, u8_t *post_auto_wnd) {

    // For GET requests to root/index, serve our custom page
    if (strncmp(http_request, "GET", 3) == 0 &&
        (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0 || strcmp(uri, "/index.shtml") == 0)) {

        // This is a bit tricky - httpd_post_begin is meant for POST, not for serving files
        // For GET requests, we should return ERR_ARG to let httpd handle it normally
        // But since httpd is falling back to default page, we need another approach

        // Actually, let me try returning ERR_OK and see if we can serve content this way
        // But httpd_post_begin expects POST data handling, not file serving

        return ERR_ARG; // Let httpd handle GET requests normally
    }

    // For POST requests, handle them normally
    if (strncmp(http_request, "POST", 4) == 0) {
        // Handle POST requests normally
        return ERR_ARG;
    }

    // For other requests, let httpd handle them
    return ERR_ARG;
}


    ctx->ap_mode_active = true;

    return true;
}

// Scan for available WiFi networks
bool wifi_setup_scan_networks(wifi_setup_context_t *ctx) {
    printf("[+] Scanning for WiFi networks...\n");

    ctx->network_count = 0;

    // Start WiFi scan
    int result = cyw43_wifi_scan(&cyw43_state, NULL, ctx, scan_result_callback);
    if (result != 0) {
        printf("[!] Failed to start WiFi scan: %d\n", result);
        return false;
    }

    // Wait for scan to complete
    int timeout = 100; // 10 seconds timeout
    while (cyw43_wifi_scan_active(&cyw43_state) && timeout > 0) {
        sleep_ms(100);
        timeout--;
    }

    if (timeout == 0) {
        printf("[!] WiFi scan timeout\n");
        return false;
    }

    printf("[+] Found %d networks\n", ctx->network_count);
    for (int i = 0; i < ctx->network_count; ++i) {
        const char *security = (ctx->networks[i].auth_mode == 0) ? "Open" : "Secured";
        printf("  %d: %-32s RSSI:%4d %s\n",
               i + 1, ctx->networks[i].ssid, ctx->networks[i].rssi, security);
    }

    return true;
}

// Get scanned networks
const wifi_network_t* wifi_setup_get_networks(wifi_setup_context_t *ctx, int *count) {
    *count = ctx->network_count;
    return ctx->networks;
}

// Save WiFi and station configuration to flash
bool wifi_setup_save_config(wifi_setup_context_t *ctx, const char *ssid, const char *password, const char *callsign, const char *locator, uint32_t frequency) {
    printf("[+] wifi_setup_save_config called with SSID: '%s', Callsign: '%s', Locator: '%s'\n", ssid, callsign, locator);

    // Convert callsign and locator to uppercase
    char upper_callsign[12] = {0};
    char upper_locator[7] = {0};

    // Copy and convert callsign to uppercase using standard C function
    for (size_t i = 0; i < sizeof(upper_callsign) - 1 && callsign[i]; i++) {
        upper_callsign[i] = toupper((unsigned char)callsign[i]);
    }

    // Copy and convert locator to uppercase using standard C function
    for (size_t i = 0; i < sizeof(upper_locator) - 1 && locator[i]; i++) {
        upper_locator[i] = toupper((unsigned char)locator[i]);
    }

    printf("[+] Converted to uppercase - Callsign: '%s', Locator: '%s'\n", upper_callsign, upper_locator);

    if (flashmem_write_config(&ctx->flash_ctx, ssid, password, upper_callsign, upper_locator, frequency)) {
        printf("[+] Configuration saved successfully\n");
        // Mark setup as complete by disabling AP mode
        printf("[+] Setting ap_mode_active = false on ctx=%p\n", ctx);
        ctx->ap_mode_active = false;
        return true;
    } else {
        printf("[!] Failed to save configuration\n");
        return false;
    }
}

// Get stored station configuration
bool wifi_setup_get_station_config(wifi_setup_context_t *ctx, char *callsign, char *locator, size_t callsign_size, size_t locator_size) {
    return flashmem_read_station_config(&ctx->flash_ctx, callsign, locator, callsign_size, locator_size);
}

// Get stored frequency configuration
bool wifi_setup_get_frequency_config(wifi_setup_context_t *ctx, uint32_t *frequency) {
    return flashmem_read_frequency_config(&ctx->flash_ctx, frequency);
}

// Stop AP mode and switch to STA mode
bool wifi_setup_stop_ap(wifi_setup_context_t *ctx) {
    printf("[+] Stopping AP mode and switching to STA mode\n");

    // Note: DHCP and DNS servers are static, so they'll be cleaned up automatically
    // when the network interface is reset

    // Disable AP mode
    cyw43_arch_disable_ap_mode();

    // Switch to STA mode
    cyw43_arch_enable_sta_mode();

    ctx->ap_mode_active = false;
    printf("[+] Switched to STA mode\n");
    return true;
}

// Process serial commands for WiFi setup
void wifi_setup_process_serial_command(const char *command) {
    if (!g_ctx || !g_ctx->ap_mode_active) {
        return;
    }

    // Remove trailing newline/carriage return
    char cmd[128];
    strncpy(cmd, command, sizeof(cmd) - 1);
    cmd[strcspn(cmd, "\r\n")] = '\0';

    if (strcmp(cmd, "scan") == 0) {
        printf("[+] Network scanning not available yet\n");
        printf("[+] Use 'connect <SSID> <password>' to manually enter credentials\n");
    }
    else if (strncmp(cmd, "connect ", 8) == 0) {
        // Parse command: "connect <ssid> <password>"
        char *args = cmd + 8;
        char *space_pos = strchr(args, ' ');

        if (space_pos) {
            *space_pos = '\0';
            const char *ssid = args;
            const char *password = space_pos + 1;

            if (strlen(ssid) > 0 && strlen(password) > 0) {
                printf("[+] Saving WiFi credentials for SSID: '%s'\n", ssid);

                // Save credentials (old serial interface - keeping for compatibility)
                if (wifi_setup_save_config(g_ctx, ssid, password, "VU3CER", "MK68", 28075500UL)) {
                    printf("[+] WiFi credentials saved successfully!\n");
                    printf("[+] Setup complete. Rebooting device...\n");

                    // Mark setup as complete
                    g_ctx->ap_mode_active = false;

                    // Reboot after a short delay
                    sleep_ms(2000);
                    printf("[+] You can now disconnect from 'Pico-FT8-Setup' WiFi\n");
                    // watchdog_reboot(0, SRAM_END, 10); // Uncomment when ready to reboot
                } else {
                    printf("[!] Failed to save credentials\n");
                }
            } else {
                printf("[!] Invalid SSID or password\n");
            }
        } else {
            printf("[!] Usage: connect <SSID> <password>\n");
            printf("[!] Example: connect MyWiFiNetwork mypassword123\n");
        }
    }
    else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        printf("[+] Pico FT8 WiFi Setup Commands:\n");
        printf("[+]   connect <ssid> <pwd>    - Save WiFi credentials and exit setup\n");
        printf("[+]   help                    - Show this help\n");
        printf("[+] \n");
        printf("[+] Example: connect MyHomeNetwork mypassword123\n");
    }
    else if (strlen(cmd) > 0) {
        printf("[!] Unknown command: %s\n", cmd);
        printf("[!] Type 'help' for available commands\n");
    }
}

// Check if WiFi credentials are stored
bool wifi_setup_has_credentials(wifi_setup_context_t *ctx) {
    char ssid[32];
    char password[64];
    return flashmem_read_wifi_config(&ctx->flash_ctx, ssid, password, sizeof(ssid), sizeof(password));
}

// Get stored WiFi credentials
bool wifi_setup_get_credentials(wifi_setup_context_t *ctx, char *ssid, char *password, size_t ssid_size, size_t password_size) {
    return flashmem_read_wifi_config(&ctx->flash_ctx, ssid, password, ssid_size, password_size);
}

// Initialize WiFi setup system
bool wifi_setup_init(wifi_setup_context_t *ctx) {
    g_ctx = ctx;
    flashmem_init(&ctx->flash_ctx);
    ctx->network_count = 0;
    ctx->ap_mode_active = false;
    printf("[+] WiFi setup system initialized\n");
    return true;
}

// Check if WiFi setup is complete
bool wifi_setup_is_setup_complete(void) {
    bool complete = g_ctx && !g_ctx->ap_mode_active;
    printf("[DEBUG] wifi_setup_is_setup_complete: g_ctx=%p, ap_mode_active=%d, returning %s\n",
           g_ctx, g_ctx ? g_ctx->ap_mode_active : -1, complete ? "true" : "false");
    if (g_ctx) {
        printf("[DEBUG] g_ctx address: %p, ap_mode_active address: %p\n", g_ctx, &g_ctx->ap_mode_active);
    }
    return complete;
}
