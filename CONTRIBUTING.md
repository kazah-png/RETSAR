# Contribuir a RETSAR

El objetivo es un proveedor capaz de crear puestos Windows independientes. El motor transaccional actual y su simulador no demuestran esa capacidad.

## Cambios verificables

1. Explica el problema y el comportamiento que cambia.
2. Conserva la separación entre el proveedor simulado, las herramientas de laboratorio y el ejecutable público.
3. Compila en Windows desde una carpeta de C: con `./build.ps1 -Test`. Incluye los resultados y los casos omitidos.
4. Actualiza `docs/PROGRESS.md` y, si cambia una decisión, `docs/ARCHITECTURE.md` o `docs/NATIVE-BACKEND.md`.
5. Presenta cambios pequeños y revisables. Un identificador de sesión existente o un dispositivo D3D no prueban la creación de un puesto nuevo.

Las pruebas que creen sesiones, registren proveedores, instalen controladores o asignen dispositivos requieren un laboratorio independiente. No se ejecutan desde la compilación, la suite automática ni los comandos de consulta.

## Procedencia y datos

Usa documentación primaria y señala qué es un hecho, una inferencia o una hipótesis. Atribuye código y dependencias respetando sus licencias. Los informes de descompilación, componentes propietarios, credenciales e inventarios personales no forman parte del repositorio.

Los registros públicos deben estar depurados. No pegues tokens, contraseñas, cookies ni archivos de cuentas en issues o pull requests. Para un resultado del laboratorio, bastan identificadores de prueba y los datos técnicos necesarios para reproducirlo.
