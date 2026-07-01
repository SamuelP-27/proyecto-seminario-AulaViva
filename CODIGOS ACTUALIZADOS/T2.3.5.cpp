// T2.3.5 - asignar un estado ("Atento"/"Distraído") a cada frame del video

// Archivo: analizador_atencion.cpp (función evaluarFrame)

TipoDistraccion tipoBruto = TipoDistraccion::Ninguna;
EstadoAtencion  estadoBruto = clasificarPose(pose, lm, bboxRostro, *ep, tipoBruto);

TipoDistraccion tipoConfirmado = tipoBruto;
EstadoAtencion  estadoConfirmado = actualizarVentana(*ep, estadoBruto, tipoConfirmado);

...
res.estado      = estadoConfirmado;
res.tipoDistrac = tipoConfirmado;

historial_.push_back(res);
return res;
