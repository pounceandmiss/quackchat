// The wiring every model that reads from the backend needs, in one place.
//
// Omitting one of these fails quietly: without `error` a read that never left
// goes unnoticed, and without `connected` a model that asked while the link was
// down never asks again - which on Android is every launch, the interpreter
// living in a service process the UI reattaches to. Hence the handler names are
// required rather than passed in.
//
// Opting out of the re-read is a decision rather than an oversight, so it has
// its own name below rather than a null argument.
#ifndef BACKENDBINDING_H
#define BACKENDBINDING_H

#include "TackyBackend.h"

// Everything but the re-read. For a model whose reads are the user's own
// questions - a search, a room browse - which must not be re-asked behind them.
template <class T>
void bindBackendWithoutReseed(T *self, TackyBackend *backend) {
    QObject::connect(backend, &TackyBackend::event, self, &T::handleEvent);
    QObject::connect(backend, &TackyBackend::result, self, &T::handleResult);
    QObject::connect(backend, &TackyBackend::error, self, &T::handleError);
}

template <class T>
void bindBackend(T *self, TackyBackend *backend, void (T::*reseed)()) {
    bindBackendWithoutReseed(self, backend);
    QObject::connect(backend, &TackyBackend::connected, self, reseed);
}

#endif // BACKENDBINDING_H
