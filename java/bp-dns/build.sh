#!/bin/sh
# Builds bp-dns.jar (Secure DNS for the Java extensions) for Java 21+; the bundle ships it in java/.
set -e
cd "$(dirname "$0")"
rm -rf out && mkdir -p out
javac --release 21 -d out src/bp/dns/DohProvider.java
cp -R src/META-INF out/
jar cf ../../packaging/java/bp-dns.jar -C out .
rm -rf out
echo "built packaging/java/bp-dns.jar"
