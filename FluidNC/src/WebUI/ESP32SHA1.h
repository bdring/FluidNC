#include <Arduino.h>
#include "mbedtls/md.h"

String sha1(String payloadStr) {
  const char *payload = payloadStr.c_str();
  int size = 20;
  byte shaResult[size];

  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA1;

  const size_t payloadLength = strlen(payload);

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *)payload, payloadLength);
  mbedtls_md_finish(&ctx, shaResult);
  mbedtls_md_free(&ctx);

  String hashStr = "";

  for (uint16_t i = 0; i < size; i++) {
    String hex = String(shaResult[i], HEX);
    if (hex.length() < 2) {
      hex = "0" + hex;
    }
    hashStr += hex;
  }

  return hashStr;
}
