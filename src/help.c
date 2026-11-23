#include "ui.h"

static const char general[] =
    "PicoCalc VNC Client\n"
    "\n"
    "Global keys:\n"
    "  F1      This help (any time)\n"
    "  F2      Not connected: toggle WiFi / VNC screens\n"
    "          Connected: special keys menu\n"
    "  F3      Disconnect from VNC server\n"
    "  F4      Left mouse button (hold to drag)\n"
    "  F5      Right mouse button\n"
    "  Arrows  Move the mouse pointer\n"
    "\n"
    "Settings are stored on the SD card in\n"
    "/picocalc-vnc.ini (passwords in plain text).\n";

static const char wifi[] =
    "WiFi screen:\n"
    "  Up/Down     Select\n"
    "  Enter       Connect to network / run action\n"
    "  D / Del     Forget a saved network\n"
    "  R           Rescan\n"
    "  U           Firmware updates\n"
    "  F2          Switch to VNC connections\n"
    "  Esc         Switch to VNC connections\n"
    "\n"
    "Networks marked * are saved. On start-up the\n"
    "strongest saved network in range is joined\n"
    "automatically (Esc skips).\n"
    "\n"
    "Use 'Hidden network...' to type an SSID and\n"
    "password for a network that is not broadcast.\n"
    "'Saved networks...' lets you connect, edit or\n"
    "delete remembered networks.\n"
    "\n"
    "Global keys:\n"
    "  F1  Help   F2  Toggle WiFi/VNC screen\n";

static const char vnclist[] =
    "VNC connections:\n"
    "  Up/Down     Select\n"
    "  Enter       Connect / Edit / Delete menu\n"
    "  E           Edit selected connection\n"
    "  D / Del     Delete selected connection\n"
    "  U           Firmware updates\n"
    "  F2 / Esc    Switch to WiFi screen\n"
    "\n"
    "Choose 'New Connection' to add a server.\n"
    "Connecting requires WiFi; you can still add,\n"
    "edit and delete entries while offline.\n"
    "\n"
    "Port: use a full port (e.g. 5900) or a display\n"
    "number below 100 (e.g. 1 = port 5901).\n"
    "Colour: 16-bit looks best, 8-bit is faster on\n"
    "slow links.\n"
    "Zoom: Fit shows the whole desktop scaled down,\n"
    "50% and 100% show a part that pans with the\n"
    "mouse pointer.\n"
    "\n"
    "Security: 'None' and 'VNC password' are\n"
    "supported. Make sure the server allows one of\n"
    "these (e.g. disable encryption-only modes).\n";

static const char form[] =
    "Editing:\n"
    "  Up/Down     Previous / next field\n"
    "  Enter       Next field / press button\n"
    "  Backspace   Delete last character\n"
    "  Del         Clear the field\n"
    "  Left/Right  Change option (< >) fields\n"
    "  Tab         Show/hide password\n"
    "  Esc         Cancel\n";

static const char session[] =
    "VNC session:\n"
    "  Arrows   Move mouse (hold to accelerate)\n"
    "  F4       Left button (hold to drag)\n"
    "  F5       Right button\n"
    "  F2       Special keys menu\n"
    "  F3       Disconnect\n"
    "  F1       This help\n"
    "\n"
    "Shift + F-keys:\n"
    "  F6 (Sh+F1)  Arrows: mouse <-> cursor keys\n"
    "  F7 (Sh+F2)  Cycle zoom: Fit / 50% / 100%\n"
    "  F8 (Sh+F3)  Refresh the screen\n"
    "  F9 (Sh+F4)  Scroll wheel up\n"
    "  F10(Sh+F5)  Scroll wheel down\n"
    "\n"
    "Typing is sent to the remote computer.\n"
    "Ctrl and Alt combine with the next key.\n"
    "Use the F2 menu for Ctrl+Alt+Del, Alt+Tab,\n"
    "Esc, Windows key, F1-F12, middle click etc.\n"
    "\n"
    "In 50%/100% zoom the view pans automatically\n"
    "when the pointer nears the screen edge.\n";

static const char updates[] =
    "Firmware updates:\n"
    "  Current shows the running firmware version.\n"
    "  Check GitHub finds the latest public release.\n"
    "  WiFi is required to check or download.\n"
    "\n"
    "Download update & reboot needs a newer release\n"
    "and a writable SD card. It verifies HTTPS,\n"
    "SHA-256 and the Pico 2 UF2 format, then saves\n"
    "the versioned UF2 under /pico2-apps/.\n"
    "\n"
    "The app restarts into UF2Loader automatically.\n"
    "Select the downloaded version to install it.\n"
    "BOOT2350.UF2 must be on the SD card.\n"
    "Existing firmware and settings are preserved.\n"
    "\n"
    "Network time (UDP 123) is needed for certificate\n"
    "date checks. Esc cancels network operations.\n"
    "F2 switches to the other connection screen.\n";

const char *help_text(int ctx) {
    switch (ctx) {
    case HELP_WIFI: return wifi;
    case HELP_VNCLIST: return vnclist;
    case HELP_FORM: return form;
    case HELP_SESSION: return session;
    case HELP_UPDATE: return updates;
    default: return general;
    }
}
