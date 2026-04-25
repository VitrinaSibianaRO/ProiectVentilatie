@echo off
echo Generare Keystore pentru Google Play...
keytool -genkey -v -keystore my-release-key.keystore -alias my-key-alias -keyalg RSA -keysize 2048 -validity 10000 -storepass password123 -keypass password123 -dname "CN=Ventilatie, OU=Dev, O=Proiect, L=City, S=State, C=RO"
echo Keystore generat! Poti muta fisierul in arhiva proiectului.
pause
