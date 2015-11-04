## Keystore

Why use a keystore?

To go from ORANGE screen to YELLOW screen. This will help ensure that even with a custom ROM, your system hasn't been compromised.
On boot, you'll get the YELLOW screen, giving you the fingerprint of the keystore.
To ensure safety, check the fingerprint is always the same.
DO NOT stop at the first letters. Better know random letters than first letters.

How to generate keystore keys?

> $AOSP_TREE/development/tools/make_key keystore '/C=US/ST=California/L=Mountain View/O=Android/OU=Android/CN=Android/emailAddress=android@android.com'

Obviously the second string is meant to be replaced.

How to generate keystore image?

> openssl rsa -in keystore.pk8 -inform der -outform der -pubout -out keystore.pub.pk8
> $AOSP_TREE/out/host/linux-x86/bin/keystore_signer keystore.pk8 keystore.x509.pem keystore.img keystore.pub.pk8
