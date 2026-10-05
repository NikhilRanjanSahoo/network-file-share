#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include "Database.h"

// Interactive console wizard that provisions the first Admin (and optionally more
// users) when the users table is empty. Lives outside Database so the data layer
// has no console I/O. Returns false if input ended before an admin was created.
bool runFirstTimeSetup(Database& db);

#endif
