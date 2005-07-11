This version of Botan was modified by Matt Johnston (matt ucc.asn.au) on
November 25 2004 for use with Monotone.

Changes are the addition of rudimentary gzip (de)compression, and the ability
to decode un-encrypted RAW_BER PKCS8 keys, as well as large amounts of path
rearrangements.



Botan: Version 1.4.3, November 6, 2004

Botan is a C++ class library for performing a wide variety of cryptographic
operations, including encryption, hashing, authentication, public key
encryption and signatures, and creating and using X.509v3 certificates and
CRLs. Import/export of PKCS #8 private keys (with optional PKCS #5 v2.0
encryption), and the creation and processing of PKCS #10 certificate requests
is also supported. Botan includes a large number of algorithms, including:

* Public Key Algorithms: Diffie-Hellman, DSA, ElGamal, Nyberg-Rueppel,
    Rabin-Williams, RSA
* Block Ciphers: AES, Blowfish, CAST-128, CAST-256, DES/DESX/TripleDES, GOST,
    IDEA, Lion, Luby-Rackoff, MISTY1, RC2, RC5, RC6, SAFER-SK, Serpent,
    Skipjack, Square, TEA, Twofish, XTEA
* Stream Ciphers: ARC4, ISAAC, SEAL, WiderWake4+1
* Hash Functions: HAS-160, HAVAL, MD2, MD4, MD5, RIPEMD-128, RIPEMD-160,
    SHA-160, SHA-256, SHA-384, SHA-512, Tiger, Whirlpool
* MACs: ANSI X9.19 MAC, HMAC, OMAC, SSL3-MAC

For build instructions, read 'doc/building.pdf'. The license can be found in
'doc/license.txt', and the ChangeLog is in 'doc/log.txt'.

Check http://botan.randombit.net/ for announcements and news. If you'll be
developing code using Botan, consider joining the mailing lists; links to
subscriptions forms and the archives can be found on the web page. Feel free to
contact me with any questions or comments.

Regards,
   Jack Lloyd (lloyd@randombit.net)
