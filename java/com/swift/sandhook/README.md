# 1. Crear carpetas temporales
mkdir -p out/classes
mkdir -p out/dex

# 2. Compilar los .java a .class
javac -source 1.8 -target 1.8 \
  -classpath android.jar \
  -d out/classes \
  $(find . -name "*.java")

# 3. Convertir los .class a classes.dex con d8
d8 \
  --classpath android.jar \
  --output out/dex \
  $(find out/classes -name "*.class")

# 4. Empaquetar el classes.dex en un .jar (o .zip)
cd out/dex
zip -r ../sandhook-java.jar classes.dex
cd ../..

# 5. Limpiar temporales (opcional)
rm -rf out/classes out/dex