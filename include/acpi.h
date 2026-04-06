// acpi.h — minimal ACPI power management interface

#ifndef VERNISOS_ACPI_H
#define VERNISOS_ACPI_H

#include <stdbool.h>
#include <stdint.h>

bool acpi_init(void);
bool acpi_ready(void);
void acpi_shutdown(void);
void acpi_reboot(void);

#endif // VERNISOS_ACPI_H
