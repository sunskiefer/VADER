/* a QR code for a short text (src/qr.c): level H, versions 1..6; modules[size*size], 1 = dark */
#pragma once
int qr_capacity(int version);                                                  /* bytes a version holds (0: none) */
int qr_encode(const char *text, int version, unsigned char *modules, int *size);   /* version 0: the smallest that fits; 0 ok */
