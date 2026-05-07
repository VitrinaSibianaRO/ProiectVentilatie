#pragma once

// ============================================================
//  HiveMqCert.h — BearSSL Trust Anchors for SSLClient.
//
//  Combined trust anchors (regenerate cu pycert_bearssl):
//   [0] USERTrust ECC Certification Authority   → github.com (OTA)
//   [1] ISRG Root X1                            → HiveMQ Cloud (MQTT 8883)
//                                              + objects.githubusercontent.com (OTA assets)
//
//  Acopera TLS pentru: MQTT HiveMQ Cloud + OTA Master + OTA Slave proxy.
// ============================================================

#include <SSLClient.h>

// Trust anchor array — definit în HiveMqCert.cpp
extern const br_x509_trust_anchor TrustAnchors[];
constexpr size_t TrustAnchors_NUM = 2;
