#include <ananas/types.h>
#include <ananas/error.h>
#include <ananas/lib.h>
#include <ananas/trace.h>
#include <ananas/vm.h>
#include <machine/smp.h>
#include <machine/apic.h>
#include <machine/ioapic.h>
#include <machine/param.h>
#include "../../dev/acpi/acpi.h"
#include "../../dev/acpi/acpica/acpi.h"

TRACE_SETUP;

extern struct IA32_SMP_CONFIG smp_config;

errorcode_t
acpi_smp_init(int* bsp_apic_id)
{
	ACPI_TABLE_MADT* madt;
	if (ACPI_FAILURE(AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt)))
		return ANANAS_ERROR(NO_DEVICE);

	/*
	 * The MADT doesn't tell us which CPU is the BSP, but we do know it is our
	 * current CPU, so just asking would be enough.
	 */
	KASSERT(madt->Address == LAPIC_BASE, "lapic base unsupported");
	vm_map_device(madt->Address, LAPIC_SIZE);
	*((volatile uint32_t*)LAPIC_SVR) |= LAPIC_SVR_APIC_EN;
	*bsp_apic_id = (*(volatile uint32_t*)LAPIC_ID) >> 24;

	/* First of all, walk through the MADT and just count everything */
	for (ACPI_SUBTABLE_HEADER* sub = (void*)(madt + 1);
	     sub < (ACPI_SUBTABLE_HEADER*)((char*)madt + madt->Header.Length);
	    sub = (ACPI_SUBTABLE_HEADER*)((char*)sub + sub->Length)) {
		switch(sub->Type) {
			case ACPI_MADT_TYPE_LOCAL_APIC:
				smp_config.cfg_num_cpus++;
				break;
			case ACPI_MADT_TYPE_IO_APIC:
				smp_config.cfg_num_ioapics++;
				break;
		}
	}

	/*
	 * ACPI interrupt overrides will only list exceptions; this means we'll
	 * have to pre-allocate all ISA interrupts and let the overrides
	 * alter them.
	 */
	smp_config.cfg_num_ints = 16;

	/* XXX Kludge together an ISA bus for the IRQ routing code */
	smp_config.cfg_num_busses = 1;

	/* Allocate tables for the resources we found */
	smp_prepare_config(&smp_config);

	/* Create the ISA bus */
	smp_config.cfg_bus[0].id = 0;
	smp_config.cfg_bus[0].type = BUS_TYPE_ISA;

	/* Identity-map all interrupts */
	for (int n = 0; n < smp_config.cfg_num_ints; n++) {
		struct IA32_INTERRUPT* interrupt = &smp_config.cfg_int[n];
		interrupt->source_no = n;
		interrupt->dest_no = n;
		interrupt->bus = &smp_config.cfg_bus[0];
		interrupt->ioapic = &smp_config.cfg_ioapic[0]; // XXX
	}

	/* Since we now got all memory set up, */
	int cur_cpu = 0, cur_ioapic = 0;
	for (ACPI_SUBTABLE_HEADER* sub = (void*)(madt + 1);
	     sub < (ACPI_SUBTABLE_HEADER*)((char*)madt + madt->Header.Length);
	    sub = (ACPI_SUBTABLE_HEADER*)((char*)sub + sub->Length)) {
		switch(sub->Type) {
			case ACPI_MADT_TYPE_LOCAL_APIC: {
				ACPI_MADT_LOCAL_APIC* lapic = (ACPI_MADT_LOCAL_APIC*)sub;
				kprintf("lapic, acpi id=%u apicid=%u\n", lapic->ProcessorId, lapic->Id);

				struct IA32_CPU* cpu = &smp_config.cfg_cpu[cur_cpu];
				cpu->lapic_id = lapic->Id;
				cur_cpu++;
				break;
			}
			case ACPI_MADT_TYPE_IO_APIC: {
				ACPI_MADT_IO_APIC* apic = (ACPI_MADT_IO_APIC*)sub;
				kprintf("ioapic, Id=%x addr=%x base=%u\n", apic->Id, apic->Address, apic->GlobalIrqBase);

				struct IA32_IOAPIC* ioapic = &smp_config.cfg_ioapic[cur_ioapic];
				ioapic->ioa_id = apic->Id;
				ioapic->ioa_addr = apic->Address;

				/* XXX Assumes the address is in kernel space (it should be) */
				vm_map_kernel(ioapic->ioa_addr, 1, VM_FLAG_READ | VM_FLAG_WRITE);

				/* Set up the IRQ source */
				ioapic_register(ioapic, apic->GlobalIrqBase);
				cur_ioapic++;
				break;
			}
			case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
				ACPI_MADT_INTERRUPT_OVERRIDE* io = (ACPI_MADT_INTERRUPT_OVERRIDE*)sub;
				kprintf("intoverride, bus=%u SourceIrq=%u globalirq=%u\n", io->Bus, io->SourceIrq, io->GlobalIrq);
				KASSERT(io->SourceIrq < smp_config.cfg_num_ints, "interrupt override out of range");

				struct IA32_INTERRUPT* interrupt = &smp_config.cfg_int[io->SourceIrq];
				interrupt->source_no = io->SourceIrq;
				interrupt->dest_no = io->GlobalIrq;

				/*
				 * Disable the identity mapping of this IRQ - this prevents entries from
			 	 * being overwritten.
				 */
				if (io->GlobalIrq != io->SourceIrq) {
					interrupt = &smp_config.cfg_int[io->GlobalIrq];
					interrupt->bus = NULL;
				}
				break;
			}
		}
	}

	return ANANAS_ERROR_OK;
}

/* vim:set ts=2 sw=2: */