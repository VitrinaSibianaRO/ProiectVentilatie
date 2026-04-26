#!/bin/bash

# Folderul de destinatie
TARGET_DIR="APK"

# Creare folder daca nu exista
mkdir -p "$TARGET_DIR" && rm -f "$TARGET_DIR"/*

echo "------------------------------------------------"
echo "Cautare si descarcare ultimul APK (Android-APK)..."
echo "------------------------------------------------"

# Descarcare folosind GitHub CLI
# --name Android-APK: numele artefactului definit in workflow
# --dir $TARGET_DIR: folderul unde se salveaza
gh run download --name Android-APK --dir "$TARGET_DIR"

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Succes! Fisierele au fost descarcate in folderul $TARGET_DIR/."
    echo "Continut folder APK:"
    ls -lh "$TARGET_DIR"
else
    echo ""
    echo "❌ Eroare: Nu s-a putut descarca artefactul."
    echo "Asigura-te ca esti autentificat (gh auth login) si ca exista cel putin un build reusit in GitHub Actions."
fi
