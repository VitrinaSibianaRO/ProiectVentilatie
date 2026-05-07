#pragma once

// ============================================================
//  HiveMqCert.h — BearSSL Trust Anchors for SSLClient.
//  Auto-generated from pycert_bearssl tool + ISRG Root X1.
//  Cert: CN=ISRG Root X1, O=Internet Security Research Group, C=US
//  Valid until: June 2035
//  Used for TLS handshake with HiveMQ Cloud (port 8883).
// ============================================================

#include <SSLClient.h>

// Trust anchor array — definit în HiveMqCert.cpp
extern const br_x509_trust_anchor TrustAnchors[];
constexpr size_t TrustAnchors_NUM = 1;
