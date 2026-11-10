# Investigación del proveedor nativo

Estado de la revisión: **10 de noviembre de 2026**. Este documento define una investigación y sus pruebas de aceptación. No describe un proveedor operativo ni instrucciones para instalar componentes en un equipo de uso diario.

**RETSAR todavía no dispone de un mecanismo validado para crear un segundo puesto simultáneo en Windows 11 con pantalla y entrada independientes.** La falta de una implementación comprobada no demuestra que cualquier implementación sea técnicamente imposible. El núcleo transaccional permanece separado de esa cuestión: sus pruebas no acreditan creación de sesiones.

## Candidato con interfaces y código públicos

Microsoft publica [Remote Desktop Services Protocol Sample](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample), un ejemplo con licencia MIT para implementar un proveedor de protocolo RDS. Su solución C++/ATL está en `Sample/TestProtocol_Ext.sln`. El proveedor entrega conexiones a RDS y puede participar en la asociación de dispositivos de entrada y de un controlador de pantalla indirecta a una sesión. No es una implementación multiseat completa.

El [README del ejemplo](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/README.md) advierte que las ediciones cliente contempladas admiten una sola sesión activa y orienta los escenarios concurrentes a Windows Server con RDSH o a Enterprise multi-session. La documentación de [IWRdsProtocolManager](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdsprotocolmanager) declara Windows Server 2012 como servidor mínimo y ningún cliente Windows compatible. La presencia de una DLL o de estas interfaces en un equipo no acredita que la edición permita el escenario buscado.

### Secuencia que debe implementar y observar el laboratorio

| Interfaz o evento | Función verificable | Lo que no acredita por sí solo |
| --- | --- | --- |
| `IWRdsProtocolManager::CreateListener` | RDS solicita el objeto que atenderá conexiones. | Una sesión creada o conectada. |
| `IWRdsProtocolListener::StartListen` | Entrega el callback de RDS al proveedor. | Disponibilidad de pantalla o entrada. |
| `IWRdsProtocolListenerCallback::OnConnected` | Comunica una conexión entrante e inicia su procesamiento por RDS. | Que se haya concedido una sesión nueva y simultánea. |
| `IWRdsProtocolConnectionCallback::OnReady` | Comunica que la conexión está preparada para continuar. | Inicio de sesión completado. |
| `IWRdsProtocolConnection::NotifySessionId` | Informa de la sesión asociada a la conexión; la asociación puede cambiar. | Propiedad permanente de un ID numérico. |
| `IWRdsProtocolConnection::LogonNotify` | Notifica el inicio de sesión del usuario. | Aislamiento de salida o entrada. |
| `IWRdsWddmIddProps::GetHardwareId` / `OnDriverLoad` | Selecciona el IDD y recibe la notificación de su carga. | Control exclusivo de una salida física. |
| `IWRdsProtocolConnection::GetInputHandles` | Proporciona los handles de entrada que consume la pila. | Que los dispositivos estén correctamente aislados. |

La relación entre servicio y proveedor está descrita en [Method Call Sequence](https://learn.microsoft.com/en-us/windows/win32/termserv/method-call-sequence). [OnConnected](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocollistenercallback-onconnected) inicia una secuencia gestionada por RDS; no es una llamada genérica para reservar un puesto local.

El archivo [TestProtocolAPI.cpp](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/Sample/TestProtocol_Ext/TestProtocolAPI.cpp) muestra `OnConnected`, `GetConnectionId` y `OnReady`. Esa secuencia sirve como referencia de integración. El disparador de prueba y el manejo de autenticación del ejemplo necesitan sustituirse por mecanismos adecuados antes de cualquier uso fuera de un laboratorio.

### Partes que el ejemplo no resuelve

En [WRdsProtocolConnection.cpp](https://github.com/Azure/Remote-Desktop-Services-Protocol-Sample/blob/main/Sample/TestProtocol_Ext/WRdsProtocolConnection.cpp), `GetInputHandles` sigue marcado como pendiente y no entrega handles de entrada operativos. `GetHardwareId` nombra un driver de ejemplo; esa cadena no equivale a disponer de su implementación instalada y validada. Los callbacks de sesión son puntos de extensión, no un registro durable de propiedad. El ejemplo requiere revisión de resultados, salidas, vida de objetos y recuperación antes de integrarlo con `SeatExecutor`.

Por tanto, compilar esa solución no permite marcar como implementadas las capacidades nativas de RETSAR. Tampoco resuelve por sí misma los requisitos de edición, licencias o autenticación de Windows.

## La pantalla depende de una sesión real

La documentación de [IddCx para sesiones remotas](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-remote-idds) establece un requisito concreto: el dispositivo de un IDD con `IDDCX_ADAPTER_FLAGS_REMOTE_SESSION_DRIVER` debe haber sido creado por la pila de escritorio remoto del sistema. `IddCxAdapterInitAsync` falla si esa condición y la bandera no coinciden. No basta con crear un monitor virtual y asociarlo a un número de sesión.

Una vez disponible el contexto correcto, el driver declara monitores y configura la topología de esa sesión mediante IddCx. [IWRdsWddmIddProps](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nn-wtsprotocol-iwrdswddmiddprops) ofrece la integración del IDD con RDS. La elección del adaptador de render puede usar [IddCxAdapterSetRenderAdapter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter), pero necesita medición: cargar un driver no demuestra aceleración GPU efectiva ni compatibilidad de una aplicación.

La documentación de [GetInputHandles](https://learn.microsoft.com/en-us/windows/win32/api/wtsprotocol/nf-wtsprotocol-iwrdsprotocolconnection-getinputhandles) describe handles de dispositivos de teclado y ratón. No ofrece una operación independiente para crear una sesión ni demuestra que una ruta concreta de entrada quede aislada. El laboratorio deberá comprobarlo de extremo a extremo.

## Entorno previo al primer experimento

El siguiente trabajo de implementación debe poder compilarse sin registrar proveedores, instalar drivers, cambiar servicios ni tocar sesiones. Su ejecución con efectos necesita un entorno separado y preparado expresamente para pruebas:

- Máquina de laboratorio o VM desechable, con recuperación comprobada y sin sesiones de uso cotidiano.
- Edición y configuración que permitan el escenario probado. Windows Server con RDSH es el candidato documentado para validar primero las interfaces. Ello no acredita soporte posterior en Windows 11.
- Cuenta de prueba, autenticación normal y permisos administrativos del laboratorio; los informes no deben contener secretos.
- Herramientas C++/ATL, SDK y WDK compatibles, y una vía de firma de drivers admitida por el entorno. El plan no depende de desactivar protecciones del equipo de uso diario.
- Registro separado de los recursos preexistentes y de los creados por la prueba, con identificadores de ejecución y operaciones durables.

Enterprise multi-session tiene condiciones específicas de despliegue; no se propone como sustitución automática de una instalación cliente. Véase la [FAQ oficial de Windows Enterprise multi-session](https://learn.microsoft.com/en-us/azure/virtual-desktop/windows-multisession-faq). Esta fase no contempla alterar políticas de concurrencia, mecanismos de licencia ni componentes de otro producto para forzar el resultado.

## Puertas de aceptación

Cada etapa debe conservar resultados fallidos y pruebas no ejercitadas. Ninguna etapa habilita por sí sola `WindowsBlockedExecutor`.

| Etapa | Evidencia exigida | Motivo para detener la progresión |
| --- | --- | --- |
| **A. Identidad de sesión** | Nueva conexión propia, sesión interactiva nueva y correlación registrada entre ejecución, conexión y sesión. La sesión principal de referencia continúa conectada. | Reutilización de una sesión ajena, cambio de identidad sin reconciliar o desconexión del puesto principal. |
| **B. Gráficos** | IDD asociado por la pila correcta, superficie que se actualiza, aplicación gráfica dentro de la sesión y adaptador/modo de render registrados. | Solo un monitor añadido al escritorio principal, ausencia de imagen o render no medido. |
| **C. Entrada** | Eventos dirigidos al puesto de prueba y observación simultánea de que el cursor y las aplicaciones del principal no reciben esos eventos. | Resultado basado únicamente en foco, coordenadas, hooks globales o una captura de pantalla. |
| **D. Recuperación** | Fallos intermedios y pérdida de respuesta conservan propiedad e incertidumbre; la retirada comprueba la identidad completa de cada recurso propio. | Liberación por ID numérico, apropiación de recursos ajenos o creación repetida después de una respuesta incierta. |
| **E. Puesto físico** | Salida física y entrada independientes en el hardware de laboratorio, conservando el primer puesto. Evidencia reproducible de creación, uso y retirada. | Solo una conexión remota visible, un escritorio alternativo o un visor de imagen. |

Las etapas A–D estudian componentes necesarios; **una sesión RDS de laboratorio todavía no cumple el hito P0 de puesto físico**. La etapa E y los criterios de [ROADMAP.md](ROADMAP.md) siguen siendo obligatorios. Una VM puede servir para estudiar el protocolo y fallos; no sustituye la validación física ni demuestra compatibilidad de juegos.

Los informes deben indicar versión y edición de Windows, configuración del laboratorio, revisiones exactas del proveedor y driver, transiciones observadas, errores originales y limpieza realizada. Una incompatibilidad se registra como resultado. No se transforma en éxito cambiando las restricciones del sistema.

## Próximo cambio C++ propuesto, todavía no implementado

Un ejecutable independiente de laboratorio, `retsar-seat-probe`, puede aportar evidencia útil antes de integrar el proveedor. Ejecutado explícitamente dentro de una sesión candidata, recogería su `SessionId`, identidad de window station/desktop y resultado de inicialización D3D11; mostraría una animación y contadores de entrada para contrastarlos con el puesto principal. El informe distinguiría datos observados de condiciones no comprobadas y permitiría correlacionar cada prueba con una ejecución.

Ese instrumento no crearía sesiones, no inyectaría eventos y no concedería recibos de propiedad por observar un ID. Sería una herramienta de aceptación para el backend futuro. Su diseño y ejecución requieren revisión; añadirlo no marcaría como completada ninguna capacidad nativa.

## Procedencia y alcance

Esta revisión usa documentación de Microsoft y el código público del ejemplo de Azure. Los enlaces al repositorio apuntan a `main`, consultado en la fecha indicada; no se ha fijado aquí un SHA verificado. Antes de reutilizar código debe registrarse la revisión exacta y conservarse su licencia. No se incorpora código del ejemplo por añadir este documento.

No hay evidencia experimental propia de este candidato en esta versión. La conclusión actual es **proveedor compatible pendiente de validar**, no compatibilidad garantizada ni imposibilidad general de multiseat en Windows.
