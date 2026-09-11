/*
 * The development peer's shell: the I/O around development_peer_core, for
 * driving a real Flipper from a development machine over a serial port
 * (FE4), or over stdin and stdout for a scripted session.
 *
 * This file is the only part of the peer that touches the operating system,
 * and it is POSIX, so it builds on Linux and under WSL where a Flipper's
 * USB CDC channel appears as a /dev/tty device. It is not part of the
 * Flipper application and is never compiled into the FAP.
 *
 * Usage:
 *   development_peer <serial-device>   drive a Flipper over that device
 *   development_peer -                 read commands from stdin, write to stdout
 *
 * Operator commands, one per line, read from stdin in both modes:
 *   ready | presenting <page> | guest <page> | terminating | recovery
 *       drive the display; <page> is wifi or guest
 *   deliver <n>          set the delivered count on the current record
 *   error <CODE>         send the current record carrying an error code
 *   wifi <payload> | url <payload>   set the demo page payloads
 *   demo on | demo off   enable or disable the button demo script
 *   resend               resend the current record
 *   malformed | oversized | partial   send a misbehaviour
 *   drop                 forget link state, as a detach would
 *   log                  print what has been received
 *   quit
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "development_peer_core.h"

static RemoteProtocolPage page_from_word(const char* word) {
    if(strcmp(word, "wifi") == 0) return RemoteProtocolPageWifi;
    if(strcmp(word, "guest") == 0) return RemoteProtocolPageGuest;
    return RemoteProtocolPageNone;
}

static RemoteProtocolError error_from_word(const char* word) {
    int error_count = 0;
    const char* const* names = remote_protocol_enumeration_values(RemoteProtocolEnumerationError, &error_count);
    for(int error_index = 0; error_index < error_count; error_index++) {
        if(strcmp(names[error_index], word) == 0) {
            return (RemoteProtocolError)error_index;
        }
    }
    return RemoteProtocolErrorNone;
}

static void print_log(const DevelopmentPeerCore* peer) {
    fprintf(stderr, "received: %u hello, %u button, %u state, %u refused\n", peer->hellos_received, peer->buttons_received, peer->states_received, peer->refusals);
    for(int entry_index = 0;; entry_index++) {
        const DevelopmentPeerLogEntry* entry = development_peer_log_entry(peer, entry_index);
        if(entry == NULL) break;
        if(entry->kind == DevelopmentPeerLogEntryMessage) {
            fprintf(stderr, "  %s\n", entry->line);
        } else {
            int error_count = 0;
            const char* const* names = remote_protocol_enumeration_values(RemoteProtocolEnumerationError, &error_count);
            fprintf(stderr, "  refused: %s\n", names[entry->error]);
        }
    }
}

/* Applies one operator command line. Returns false to quit. */
static bool apply_command(DevelopmentPeerCore* peer, char* line) {
    char* verb = strtok(line, " \t\r\n");
    if(verb == NULL) return true;
    char* argument = strtok(NULL, " \t\r\n");

    if(strcmp(verb, "quit") == 0) return false;
    if(strcmp(verb, "ready") == 0) {
        development_peer_set_display(peer, RemoteProtocolStatusReady, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
    } else if(strcmp(verb, "presenting") == 0 || strcmp(verb, "guest") == 0) {
        RemoteProtocolStatus status = strcmp(verb, "guest") == 0 ? RemoteProtocolStatusGuestConnected : RemoteProtocolStatusPresenting;
        RemoteProtocolPage page = page_from_word(argument == NULL ? "wifi" : argument);
        const char* payload = page == RemoteProtocolPageWifi ? peer->behaviour.demo_wifi_payload : peer->behaviour.demo_guest_payload;
        development_peer_set_display(peer, status, page, payload, peer->current_display.fields[RemoteProtocolDisplayFieldDelivered].integer, RemoteProtocolErrorNone);
    } else if(strcmp(verb, "terminating") == 0) {
        development_peer_set_display(peer, RemoteProtocolStatusTerminating, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
    } else if(strcmp(verb, "recovery") == 0) {
        development_peer_set_display(peer, RemoteProtocolStatusRecoveryRequired, RemoteProtocolPageNone, "", 0, RemoteProtocolErrorNone);
    } else if(strcmp(verb, "deliver") == 0 && argument != NULL) {
        uint32_t count = (uint32_t)strtoul(argument, NULL, 10);
        RemoteProtocolMessage* record = &peer->current_display;
        development_peer_set_display(peer, (RemoteProtocolStatus)record->fields[RemoteProtocolDisplayFieldStatus].integer, (RemoteProtocolPage)record->fields[RemoteProtocolDisplayFieldPage].integer, record->fields[RemoteProtocolDisplayFieldPayload].text, count, RemoteProtocolErrorNone);
    } else if(strcmp(verb, "error") == 0 && argument != NULL) {
        development_peer_send_error(peer, error_from_word(argument));
    } else if(strcmp(verb, "wifi") == 0 && argument != NULL) {
        snprintf(peer->behaviour.demo_wifi_payload, sizeof(peer->behaviour.demo_wifi_payload), "%s", argument);
    } else if(strcmp(verb, "url") == 0 && argument != NULL) {
        snprintf(peer->behaviour.demo_guest_payload, sizeof(peer->behaviour.demo_guest_payload), "%s", argument);
    } else if(strcmp(verb, "demo") == 0) {
        peer->behaviour.demo_script = argument != NULL && strcmp(argument, "on") == 0;
    } else if(strcmp(verb, "resend") == 0) {
        development_peer_resend_display(peer);
    } else if(strcmp(verb, "malformed") == 0) {
        development_peer_send_malformed(peer);
    } else if(strcmp(verb, "oversized") == 0) {
        development_peer_send_oversized(peer);
    } else if(strcmp(verb, "partial") == 0) {
        development_peer_send_partial_record(peer);
    } else if(strcmp(verb, "drop") == 0) {
        development_peer_link_dropped(peer);
    } else if(strcmp(verb, "log") == 0) {
        print_log(peer);
    } else {
        fprintf(stderr, "unknown command: %s\n", verb);
    }
    return true;
}

static int open_serial_device_quiet(const char* path, bool quiet) {
    int descriptor = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(descriptor < 0) {
        /* A node that simply does not exist is not worth reporting while
         * probing every candidate; a real permission or busy error is. */
        if(!quiet || (errno != ENOENT && errno != ENXIO)) {
            fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        }
        return -1;
    }
    struct termios settings;
    if(tcgetattr(descriptor, &settings) != 0) {
        fprintf(stderr, "cannot read terminal settings: %s\n", strerror(errno));
        close(descriptor);
        return -1;
    }
    cfmakeraw(&settings);
    /* The CDC channel ignores the line speed, but a value is required. */
    cfsetispeed(&settings, B115200);
    cfsetospeed(&settings, B115200);
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if(tcsetattr(descriptor, TCSANOW, &settings) != 0) {
        fprintf(stderr, "cannot set terminal settings: %s\n", strerror(errno));
        close(descriptor);
        return -1;
    }
    return descriptor;
}

static int open_serial_device(const char* path) {
    return open_serial_device_quiet(path, false);
}

/* Drains the peer's output to the device and reads whatever the device has
 * sent into the peer. Non blocking. Returns false when the device has gone
 * away (a cable pull: write or read fails with ENXIO, EIO and the like), so
 * the caller can reopen it. */
static bool pump(DevelopmentPeerCore* peer, int device) {
    uint8_t buffer[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
    size_t to_send = development_peer_take_output(peer, buffer, sizeof(buffer));
    size_t sent = 0;
    while(sent < to_send) {
        ssize_t written = write(device, buffer + sent, to_send - sent);
        if(written < 0) {
            if(errno == EAGAIN) continue;
            return false;
        }
        sent += (size_t)written;
    }
    ssize_t received = read(device, buffer, sizeof(buffer));
    if(received > 0) {
        development_peer_feed(peer, buffer, (size_t)received);
    } else if(received < 0 && errno != EAGAIN) {
        return false;
    }
    return true;
}

/* Whether a buffer contains the start of a HELLO line, which only the
 * peripheral's application channel sends. The command line channel answers a
 * DTR assert with its own banner, which never contains this. */
static bool looks_like_hello(const uint8_t* buffer, size_t length) {
    static const char needle[] = "HELLO version=";
    size_t needle_length = sizeof(needle) - 1;
    if(length < needle_length) return false;
    for(size_t start = 0; start + needle_length <= length; start++) {
        if(memcmp(buffer + start, needle, needle_length) == 0) return true;
    }
    return false;
}

/* Probes one serial node: opens it (which asserts DTR and makes the
 * application send its HELLO), reads briefly, and if a HELLO arrives keeps the
 * node open and feeds every byte read into the peer, so the HELLO that
 * identified the channel is the one the handshake sees rather than being
 * consumed. Returns the open descriptor, or -1 if this is not the
 * application (closing it first). */
static int probe_node(DevelopmentPeerCore* peer, const char* path) {
    int device = open_serial_device_quiet(path, true);
    if(device < 0) return -1;
    uint8_t buffer[512];
    for(int attempt = 0; attempt < 10; attempt++) {
        usleep(100000);
        ssize_t received = read(device, buffer, sizeof(buffer));
        if(received > 0 && looks_like_hello(buffer, (size_t)received)) {
            development_peer_feed(peer, buffer, (size_t)received);
            return device;
        }
    }
    close(device);
    return -1;
}

/*
 * Finds the peripheral's application channel and returns it open. The serial
 * node number is not stable across a reattachment on this platform (the
 * evaluation log records it, and the real appliance uses a udev rule matching
 * the device by serial and interface). A development tool cannot assume a
 * fixed node, so it probes each ttyACM node for the HELLO only the application
 * sends. A preferred path is tried first. Blocks until the application is
 * found, so the peer survives a cable pull without being restarted.
 */
#define MAX_SERIAL_NODE 16

static int find_application_channel(DevelopmentPeerCore* peer, const char* preferred, char* found_path, size_t found_capacity) {
    for(;;) {
        if(preferred != NULL && preferred[0] != '\0') {
            int device = probe_node(peer, preferred);
            if(device >= 0) {
                snprintf(found_path, found_capacity, "%s", preferred);
                return device;
            }
        }
        for(int node_index = 0; node_index < MAX_SERIAL_NODE; node_index++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/ttyACM%d", node_index);
            int device = probe_node(peer, path);
            if(device >= 0) {
                snprintf(found_path, found_capacity, "%s", path);
                return device;
            }
        }
        usleep(300000);
    }
}

/* Reopens the application channel after the device has disappeared. The peer's
 * link state is discarded, exactly as the appliance would on a detach, so the
 * reconnection handshakes afresh. Returns the new descriptor and updates the
 * path, since the node number may have changed. */
static int reopen_device(DevelopmentPeerCore* peer, char* path, size_t path_capacity, int old_device) {
    if(old_device >= 0) close(old_device);
    development_peer_link_dropped(peer);
    fprintf(stderr, "device gone; searching for the peripheral...\n");
    int device = find_application_channel(peer, path, path, path_capacity);
    fprintf(stderr, "peripheral back on %s\n", path);
    return device;
}

int main(int argument_count, char** arguments) {
    if(argument_count != 2) {
        fprintf(stderr, "usage: %s <serial-device>|auto|-\n", arguments[0]);
        fprintf(stderr, "  a device path drives that node; auto finds the peripheral by probing;\n");
        fprintf(stderr, "  - reads operator commands from stdin for a scripted session.\n");
        return 2;
    }
    static DevelopmentPeerCore peer;
    development_peer_initialise(&peer);

    bool use_stdio = strcmp(arguments[1], "-") == 0;
    /* The node number is not stable across a reattachment, so the current
     * path is held in a mutable buffer and updated on every reconnection. */
    char device_path[32];
    snprintf(device_path, sizeof(device_path), "%s", arguments[1]);
    int device = -1;
    if(!use_stdio) {
        bool find_by_probe = strcmp(arguments[1], "auto") == 0;
        if(find_by_probe) {
            fprintf(stderr, "searching for the peripheral...\n");
            device = find_application_channel(&peer, NULL, device_path, sizeof(device_path));
        } else {
            device = open_serial_device(arguments[1]);
        }
        if(device < 0) return 1;
        if(fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0) {
            fprintf(stderr, "cannot make stdin non blocking: %s\n", strerror(errno));
            close(device);
            return 1;
        }
        fprintf(stderr, "peer on %s. Type commands; 'quit' to exit.\n", device_path);
    }

    char command_line[512];
    bool running = true;
    while(running) {
        if(use_stdio) {
            if(fgets(command_line, sizeof(command_line), stdin) == NULL) break;
            running = apply_command(&peer, command_line);
            /* In stdio mode the peer's output is the appliance to peripheral
             * stream, printed for a scripted check. */
            uint8_t buffer[DEVELOPMENT_PEER_OUTPUT_CAPACITY];
            size_t length = development_peer_take_output(&peer, buffer, sizeof(buffer));
            fwrite(buffer, 1, length, stdout);
            fflush(stdout);
        } else {
            if(!pump(&peer, device)) {
                device = reopen_device(&peer, device_path, sizeof(device_path), device);
            }
            ssize_t read_count = read(STDIN_FILENO, command_line, sizeof(command_line) - 1);
            if(read_count > 0) {
                command_line[read_count] = '\0';
                char* saveptr = NULL;
                for(char* one = strtok_r(command_line, "\n", &saveptr); one != NULL; one = strtok_r(NULL, "\n", &saveptr)) {
                    if(!apply_command(&peer, one)) {
                        running = false;
                    }
                }
            }
            usleep(2000);
        }
    }

    if(device >= 0) close(device);
    return 0;
}
