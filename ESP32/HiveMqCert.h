#pragma once

// ============================================================
//  HiveMqCert.h
//  ISRG Root X1 — Let's Encrypt root certificate.
//  Valabil până în iunie 2035. Folosit pentru TLS handshake
//  cu HiveMQ Cloud (port 8883).
//  Stocat în PROGMEM pentru a nu consuma RAM intern.
// ============================================================

#include <pgmspace.h>

extern const char HIVEMQ_ROOT_CA[] PROGMEM;
