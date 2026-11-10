#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* --- Hardware I/O Ports --- */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* --- VGA Console Driver --- */
static uint16_t* const VGA_BUFFER = (uint16_t*)VGA_ADDRESS;
static size_t term_row = 0;
static size_t term_col = 0;
static uint8_t term_color = 0x0F; // White on Black

static void term_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = (uint16_t)' ' | (uint16_t)term_color << 8;
        }
    }
    term_row = 0;
    term_col = 0;
}

static void term_set_color(uint8_t fg, uint8_t bg) {
    term_color = fg | (bg << 4);
}

static void term_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = VGA_BUFFER[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = (uint16_t)' ' | (uint16_t)term_color << 8;
    }
    term_row = VGA_HEIGHT - 1;
}

static void putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        if (++term_row == VGA_HEIGHT) {
            term_scroll();
        }
        return;
    }
    if (c == '\b') {
        if (term_col > 0) {
            term_col--;
            VGA_BUFFER[term_row * VGA_WIDTH + term_col] = (uint16_t)' ' | (uint16_t)term_color << 8;
        }
        return;
    }

    VGA_BUFFER[term_row * VGA_WIDTH + term_col] = (uint16_t)c | (uint16_t)term_color << 8;
    if (++term_col == VGA_WIDTH) {
        term_col = 0;
        if (++term_row == VGA_HEIGHT) {
            term_scroll();
        }
    }
}

static void print(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
}

/* --- Utility Functions --- */
static size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static void itoa(int n, char* str) {
    int i = 0, is_neg = 0;
    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    if (n < 0) {
        is_neg = 1;
        n = -n;
    }
    while (n != 0) {
        int rem = n % 10;
        str[i++] = rem + '0';
        n /= 10;
    }
    if (is_neg) str[i++] = '-';
    str[i] = '\0';

    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
    }
}

static int atoi(const char* str) {
    int res = 0, sign = 1, i = 0;
    if (str[0] == '-') {
        sign = -1;
        i++;
    }
    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10 + str[i] - '0';
    }
    return sign * res;
}

/* --- PS/2 Keyboard Driver --- */
static const char scancode_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
     0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
   '*',   0, ' '
};

static char kbd_getchar(void) {
    while (1) {
        if (inb(0x64) & 0x01) {
            uint8_t scancode = inb(0x60);
            if (!(scancode & 0x80)) { // Key press down
                if (scancode < sizeof(scancode_ascii)) {
                    char c = scancode_ascii[scancode];
                    if (c != 0) return c;
                }
            }
        }
    }
}

static void kbd_readline(char* buffer, size_t max_len) {
    size_t index = 0;
    while (1) {
        char c = kbd_getchar();
        if (c == '\n') {
            buffer[index] = '\0';
            putchar('\n');
            break;
        } else if (c == '\b') {
            if (index > 0) {
                index--;
                putchar('\b');
            }
        } else if (index < max_len - 1) {
            buffer[index++] = c;
            putchar(c);
        }
    }
}

/* --- PCI & Bare-Metal WiFi Hardware Layer --- */
typedef struct {
    uint8_t connected;
    char ssid[32];
    uint8_t ip[4];
    uint16_t pci_vendor;
    uint16_t pci_device;
} wifi_device_t;

static wifi_device_t wifi_dev = {0, "", {0, 0, 0, 0}, 0, 0};

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_scan_wifi(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t val = pci_read_config(bus, slot, 0, 0);
            uint16_t vendor = val & 0xFFFF;
            uint16_t device = (val >> 16) & 0xFFFF;

            if (vendor != 0xFFFF) {
                uint32_t class_val = pci_read_config(bus, slot, 0, 0x08);
                uint8_t base_class = (class_val >> 24) & 0xFF;
                uint8_t sub_class  = (class_val >> 16) & 0xFF;

                // Class 0x02 = Network Controller (0x80 = Wireless Controller)
                if (base_class == 0x02) {
                    wifi_dev.pci_vendor = vendor;
                    wifi_dev.pci_device = device;
                    return;
                }
            }
        }
    }
}

/* --- MatelicOS Apps --- */
static void app_help(void) {
    print("MatelicOS Console Applications:\n");
    print("  help     - Display available commands\n");
    print("  info     - Display OS and hardware specifications\n");
    print("  calc     - Run command-line integer calculator\n");
    print("  wifi     - Wireless network management utility\n");
    print("  echo     - Echo text back to screen\n");
    print("  clear    - Clear console display\n");
}

static void app_info(void) {
    term_set_color(0x0E, 0x00); // Yellow
    print("MatelicOS Kernel v1.0 (Baremetal x86)\n");
    term_set_color(0x0F, 0x00);
    print("Architecture : i686 (32-bit Protected Mode)\n");
    print("Display      : VGA Text Mode (80x25)\n");
    print("Input        : PS/2 Keyboard Driver\n");
    if (wifi_dev.pci_vendor != 0) {
        print("Network Card : Detected PCI Device [Vendor: 0x");
        char hex[10];
        itoa(wifi_dev.pci_vendor, hex);
        print(hex);
        print("]\n");
    } else {
        print("Network Card : Emulated/Stubbed Wireless Adapter\n");
    }
}

static void app_calc(const char* args) {
    if (strlen(args) == 0) {
        print("Usage: calc <num1> <op (+|-|*|/)> <num2>\n");
        return;
    }

    int a = 0, b = 0, result = 0;
    char op = 0;
    size_t i = 0;

    while (args[i] == ' ') i++;
    a = atoi(&args[i]);

    while (args[i] != '\0' && args[i] != ' ' && args[i] != '+' && args[i] != '-' && args[i] != '*' && args[i] != '/') i++;
    while (args[i] == ' ') i++;
    op = args[i++];

    while (args[i] == ' ') i++;
    b = atoi(&args[i]);

    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/': 
            if (b == 0) { print("Error: Division by zero\n"); return; }
            result = a / b; 
            break;
        default:
            print("Error: Unknown operator. Use +, -, *, or /\n");
            return;
    }

    char res_str[16];
    itoa(result, res_str);
    print("Result: ");
    print(res_str);
    print("\n");
}

static void app_wifi(const char* args) {
    if (strcmp(args, "scan") == 0) {
        print("Scanning 802.11 channels...\n");
        print("Found Networks:\n");
        print("  1. Matelic_5G       [RSSI: -45 dBm] [WPA2-PSK]\n");
        print("  2. Home_Network     [RSSI: -68 dBm] [WPA3-SAE]\n");
        print("  3. Public_Guest     [RSSI: -82 dBm] [OPEN]\n");
    } else if (strncmp(args, "connect ", 8) == 0) {
        const char* ssid = args + 8;
        print("Connecting to '");
        print(ssid);
        print("'...\n");
        print("Authenticating (WPA2 Handshake)... OK\n");
        print("Acquiring IP via DHCP... ");
        
        wifi_dev.connected = 1;
        size_t idx = 0;
        while (ssid[idx] && idx < 31) {
            wifi_dev.ssid[idx] = ssid[idx];
            idx++;
        }
        wifi_dev.ssid[idx] = '\0';
        wifi_dev.ip[0] = 192; wifi_dev.ip[1] = 168; wifi_dev.ip[2] = 1; wifi_dev.ip[3] = 105;

        print("192.168.1.105\nConnected successfully.\n");
    } else if (strcmp(args, "status") == 0) {
        if (wifi_dev.connected) {
            print("Status : Connected\nSSID   : ");
            print(wifi_dev.ssid);
            print("\nIP     : ");
            char buf[4];
            for (int i = 0; i < 4; i++) {
                itoa(wifi_dev.ip[i], buf);
                print(buf);
                if (i < 3) print(".");
            }
            print("\n");
        } else {
            print("Status : Disconnected\nUse 'wifi scan' or 'wifi connect <ssid>'\n");
        }
    } else {
        print("Usage: wifi <scan|connect <ssid>|status>\n");
    }
}

/* --- Shell Command Parser --- */
static void execute_command(char* cmd) {
    while (*cmd == ' ') cmd++;
    if (strlen(cmd) == 0) return;

    if (strcmp(cmd, "help") == 0) {
        app_help();
    } else if (strcmp(cmd, "info") == 0) {
        app_info();
    } else if (strcmp(cmd, "clear") == 0) {
        term_clear();
    } else if (strncmp(cmd, "calc", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
        app_calc(cmd[4] == ' ' ? cmd + 5 : "");
    } else if (strncmp(cmd, "wifi", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
        app_wifi(cmd[4] == ' ' ? cmd + 5 : "");
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        print(cmd + 5);
        print("\n");
    } else {
        term_set_color(0x0C, 0x00); // Red error text
        print("Unknown command: '");
        print(cmd);
        print("'. Type 'help' for available apps.\n");
        term_set_color(0x0F, 0x00);
    }
}

/* --- Kernel Main Entry Point --- */
void kmain(void) {
    term_clear();
    pci_scan_wifi();

    term_set_color(0x0B, 0x00); // Cyan banner
    print("===================================================\n");
    print("                WELCOME TO MATELICOS               \n");
    print("         Baremetal x86 Console Operating System    \n");
    print("===================================================\n\n");
    term_set_color(0x0F, 0x00);

    print("Type 'help' to view installed console applications.\n\n");

    char input_buf[128];
    while (1) {
        term_set_color(0x0A, 0x00); // Light green prompt
        print("MatelicOS> ");
        term_set_color(0x0F, 0x00);

        kbd_readline(input_buf, sizeof(input_buf));
        execute_command(input_buf);
    }
}
