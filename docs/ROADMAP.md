# Hoja de ruta

RETSAR persigue dos puestos Windows independientes con asignación explícita de pantalla y entrada. Esta hoja de ruta usa criterios de aceptación; no representa un porcentaje de avance ni promete una fecha de entrega.

## Base disponible · 0.2.0-engine-preview

- [x] Inventario de solo lectura y salida JSON.
- [x] Validación de configuraciones y rechazo de asignaciones duplicadas.
- [x] Planes temporales, reservas y tokens de un solo uso.
- [x] Intenciones persistentes antes de adquirir o liberar recursos.
- [x] Recibos de propiedad y compensación en orden inverso.
- [x] Recuperación conservadora tras reiniciar y resultados inciertos.
- [x] Pruebas con proveedor simulado separado del ejecutable público.
- [x] Compilación y pruebas automatizables en Windows.

La base completa no equivale a un puesto funcional. El siguiente hito es la dependencia crítica del proyecto.

## P0 · Demostrar un puesto nativo en un entorno de prueba

- [ ] Identificar y documentar un mecanismo viable para crear una sesión interactiva nueva.
- [ ] Demostrar su identidad y ciclo de vida sin depender de una sesión ya creada por otro producto.
- [ ] Asignar una salida física y entrada independientes sin apropiarse de recursos existentes.
- [ ] Retirar únicamente los recursos de la prueba, también ante fallos intermedios.
- [ ] Reproducir el resultado y dejar evidencia revisable del entorno y sus limitaciones.

**Criterio de aceptación:** una prueba aislada crea y retira un segundo puesto real, preserva el primero y permite verificar a qué ejecución pertenece cada recurso. Lanzar un proceso en otra sesión o mostrar una captura no satisface este criterio.

## P1 · Integrar el proveedor Windows

- [ ] Implementar el contrato `SeatExecutor` con comprobaciones de propiedad atómicas.
- [ ] Vincular pantalla y entrada a la sesión exacta creada por la misma transacción.
- [ ] Hacer idempotentes las operaciones y consultar sus resultados tras una caída.
- [ ] Invalidar planes ante interferencias externas sin invalidar los efectos propios de la transacción.
- [ ] Pasar pruebas de recursos ajenos, IDs reutilizados, pérdidas de respuesta y reinicios del proveedor.

**Criterio de aceptación:** el proveedor real satisface los mismos invariantes de seguridad que exige el núcleo. Las pruebas simuladas se conservan; no sustituyen las del proveedor real.

## P2 · Uso supervisado

- [ ] Resolver la selección y conmutación de teclado/ratón entre puestos.
- [ ] Añadir una interfaz de configuración que refleje el estado real del motor.
- [ ] Diseñar instalación, retirada y actualizaciones de los componentes que finalmente sean necesarios.
- [ ] Probar recursos compartidos, suspensión, desconexiones y errores de aplicaciones.

**Criterio de aceptación:** se pueden preparar, iniciar, detener y revisar dos puestos con procedimientos de recuperación claros y sin configuraciones ocultas.

## P3 · Estabilidad y distribución

- [ ] Ejecutar pruebas prolongadas y medir CPU, memoria y GPU.
- [ ] Documentar versiones de Windows y hardware realmente comprobados.
- [ ] Resolver crecimiento y mantenimiento del diario sin perder propiedad pendiente.
- [ ] Revisar empaquetado, procedencia del código y reproducibilidad de las entregas.

No se anuncia compatibilidad con aplicaciones o juegos concretos hasta contar con pruebas reales. Las futuras afirmaciones de rendimiento deben incluir condiciones y medidas.

## Cómo actualizar esta hoja

Marca un punto como terminado solo cuando exista código revisable o evidencia reproducible que cumpla su criterio. Añade el resultado al [progreso](PROGRESS.md), incluyendo pruebas omitidas y limitaciones. Un diseño o una interfaz simulada no debe registrarse como funcionalidad nativa terminada.
