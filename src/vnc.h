#pragma once
#include <stdbool.h>
#include "config.h"

// Runs a VNC session until the user disconnects (returns true) or an error
// occurs (returns false with a message in err). The entry's zoom may be
// updated if the user changes it during the session.
bool vnc_session(vnc_entry_t *e, char *err, int errlen);
