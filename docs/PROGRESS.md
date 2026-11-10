# Progreso verificable

## 10 de noviembre de 2026 · Base transaccional 0.2.0-engine-preview

Esta es la validación local inicial, anterior al instrumento de laboratorio. No se reconstruye un historial de lanzamientos ni se presenta una prueba simulada como un puesto operativo.

| Comprobación | Resultado local |
| --- | --- |
| Modelo, validaciones y capacidades | 25 aprobadas |
| Ciclo de vida, diario y recuperación | 57 aprobadas |
| Contratos del CLI y salida de archivos | 6 aprobadas |
| **Total de comprobaciones** | **88 aprobadas, 0 fallidas** |
| JSON interpretado de forma independiente por PowerShell | Aprobado, adicional al total anterior |
| Rama adversarial de sustitución mediante junction | **1 no ejercitada** |
| Compilación x64, C++17, `/W4 /WX` | Terminada sin errores |

La prueba adversarial intentó modificar una carpeta temporal propia de C: para convertirla en una junction hacia otra carpeta temporal propia de C:. Windows rechazó la modificación con código **5 — acceso denegado**. Por ello no se comprobó dinámicamente la rama en la que la conversión llega a completarse. No cuenta como aprobada.

## Qué se comprobó

- Exclusión de escritores del diario, incluida una prueba entre dos procesos.
- Rechazo de corrupción, registros parciales, secuencias inválidas y reapertura accidental de un diario inexistente.
- Reserva de recursos, límites de configuración, tokens alterados o caducados y cambios externos de inventario.
- Arranque y compensación con el proveedor simulado; vínculos a la sesión exacta del mismo propietario.
- Resultados inciertos, fallos de escritura, pérdida de recibos, reinicio del motor e IDs de recursos reutilizados.
- Recuperación de solo lectura y presentación conservadora de estados históricos.
- Escritura del esquema RETSAR y lectura del identificador anterior sin reescribir el diario.
- Estado del proveedor Windows bloqueado y archivos nuevos del CLI sin sobrescritura.

El ejecutable público se compiló con el núcleo transaccional. El proveedor simulado solo se compiló para las pruebas. No se ejecutó inventario de dispositivos ni se crearon o modificaron puestos Windows durante esta suite.

## Qué no demuestra este resultado

- Creación de un segundo puesto Windows real.
- Separación de pantallas o de eventos de entrada entre usuarios.
- Funcionamiento de aplicaciones concretas, varias cuentas simultáneas o estabilidad prolongada.
- Recuperación frente a cualquier combinación de corte eléctrico, fallo de hardware o modificación maliciosa del diario.

## Reproducir

En una carpeta ordinaria de C:, con los requisitos de [compilación](../README.md#empezar):

```powershell
.\build.ps1 -Test
```

La suite imprime cada resultado y distingue las ramas omitidas. Los artefactos temporales se crean en `build/`; los informes del CLI, en `reports/`. Esas carpetas no forman parte del código publicado. El JSON de validación del CLI incluye el hash del ejecutable local comprobado.

## Automatización pública

[Windows CI](../.github/workflows/windows.yml) compila y ejecuta la misma suite en un entorno alojado por GitHub. Copia únicamente fuentes, pruebas y script de compilación a una carpeta temporal verificada de C: para respetar la restricción de archivos del motor.

La incorporación del flujo no equivale a una ejecución aprobada: los resultados públicos se consultan en la pestaña **Actions**. La evidencia local anterior se mantiene separada de los futuros resultados de CI.

## Investigación de sesiones del 10 de noviembre

El análisis estático identificó coordinación de sesiones existentes, comunicación con un controlador e inicio de procesos dentro de una sesión indicada. La llamada indirecta investigada se resolvió como una actualización del identificador de consola activa. Esta rama no aportó un contrato verificado para crear una nueva sesión interactiva independiente; el hallazgo tampoco demuestra que esa creación sea imposible por otras vías.

No se ejecutaron las operaciones investigadas ni se modificaron sesiones del sistema. Este resumen describe el comportamiento observado; no incorpora binarios, descompilaciones ni protocolos privados al proyecto. La investigación no añade comprobaciones al total de 88 ni acredita capacidades operativas de Windows.

Para avanzar falta identificar y validar, en un entorno independiente, una operación que cree una sesión interactiva con identidad y propiedad comprobables, mantenga operativo el puesto existente y permita retirar únicamente la instancia creada.

## Próximo punto de control

Demostrar el [hito P0](ROADMAP.md): un segundo puesto real en un entorno de prueba independiente, con propiedad verificable, retirada segura y preservación del puesto existente. Mientras no se alcance, las capacidades nativas siguen marcadas como `NOT_IMPLEMENTED`.

## 10 de noviembre de 2026 · Sonda de laboratorio y publicación

Se añadió `RETSAR-seat-probe`, separado del ejecutable principal. La compilación `build.ps1 -Test -Lab` terminó con **117 comprobaciones locales aprobadas, 0 fallidas**, JSON externo validado y una rama de junction no ejercitada. El incremento respecto a la base es de **29 pruebas puras** de argumentos y de la condición que compara las sesiones declaradas.

La sonda compiló con `/W4 /WX` y se ejecutó únicamente su ayuda. **No se ejecutó su modo interactivo**, no se abrió su ventana y no se probó la creación de D3D11 en esta fase. Su informe declara que no creó una sesión y que la independencia no está probada. La revisión eliminó una posible activación involuntaria de ventana y aclaró que contar mensajes no verifica su origen.

La primera ejecución pública de CI detectó una selección incorrecta del SDK: una carpeta de versión no contenía `crtdbg.h`. El script ahora exige cabeceras UCRT, bibliotecas x64 y herramienta de manifiesto antes de seleccionar una versión. La [segunda ejecución pública](https://github.com/kazah-png/RETSAR/actions/runs/42794971422), sobre el commit `96cc60ce09b44f6c245b1df4b67bfbe8eba9edd4`, aprobó **89 comprobaciones de la base**: 25 del modelo, 58 de ciclo de vida y 6 CLI, más el JSON independiente. En ese entorno de GitHub sí se ejercitó y aprobó la rama de junction; no cambia el resultado omitido en el equipo local.

El workflow ampliado compila la sonda y ejecuta sus pruebas puras y ayuda, sin modo interactivo. Su resultado debe consultarse para el commit concreto; no se infiere del resultado anterior.

Seguimiento abierto: [crear una sesión propia](https://github.com/kazah-png/RETSAR/issues/1) y [demostrar pantalla/entrada independientes](https://github.com/kazah-png/RETSAR/issues/2). El hito P0 sigue pendiente y el proveedor Windows permanece bloqueado.
