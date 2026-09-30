#include <ntddk.h>

#define PCI_DRV_NAME   L"\\Device\\PciConfigReader"
#define PCI_DRV_DOS    L"\\DosDevices\\PciConfigReader"

#define IOCTL_PCI_READ_CONFIG \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define PCI_CONFIG_ADDR_PORT ((PULONG)(ULONG_PTR)0xCF8)
#define PCI_CONFIG_DATA_PORT ((PULONG)(ULONG_PTR)0xCFC)

typedef struct _PCI_READ_REQUEST {
    ULONG BusNumber;
    ULONG DeviceNumber;
    ULONG FunctionNumber;
    ULONG Offset;
    ULONG Length;
} PCI_READ_REQUEST, *PPCI_READ_REQUEST;

typedef struct _PCI_READ_RESPONSE {
    ULONG Length;
    UCHAR Data[256];
} PCI_READ_RESPONSE, *PPCI_READ_RESPONSE;

DRIVER_DISPATCH PciReadDispatch;
DRIVER_DISPATCH PciCreateClose;
DRIVER_UNLOAD PciDriverUnload;

static ULONG ReadConfigDword(ULONG bus, ULONG device, ULONG function, ULONG offset)
{
    ULONG addr = 0x80000000UL |
                 ((bus & 0xFF) << 16) |
                 ((device & 0x1F) << 11) |
                 ((function & 0x07) << 8) |
                 (offset & 0xFC);

    WRITE_PORT_ULONG(PCI_CONFIG_ADDR_PORT, addr);
    return READ_PORT_ULONG(PCI_CONFIG_DATA_PORT);
}

static VOID PciDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING dos;
    UNREFERENCED_PARAMETER(DriverObject);
    RtlInitUnicodeString(&dos, PCI_DRV_DOS);
    IoDeleteSymbolicLink(&dos);
    IoDeleteDevice(DriverObject->DeviceObject);
}

static NTSTATUS PciCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS PciReadDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    PCHAR inBuf = (PCHAR)Irp->AssociatedIrp.SystemBuffer;
    PCHAR outBuf = (PCHAR)Irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG info = 0;

    if (stack->Parameters.DeviceIoControl.IoControlCode != IOCTL_PCI_READ_CONFIG)
        goto done;

    if (inBuf == NULL || inLen < sizeof(PCI_READ_REQUEST) ||
        outLen < sizeof(PCI_READ_RESPONSE))
        goto done;

    {
        PCI_READ_REQUEST req;
        PPCI_READ_RESPONSE resp = (PPCI_READ_RESPONSE)outBuf;

        RtlCopyMemory(&req, inBuf, sizeof(req));

        ULONG length = req.Length > 256 ? 256 : req.Length;
        ULONG count = 0;

        for (ULONG i = 0; i < length && (req.Offset + i) < 256; ++i) {
            ULONG off = req.Offset + i;
            ULONG dword = ReadConfigDword(req.BusNumber, req.DeviceNumber,
                                          req.FunctionNumber, off & ~3UL);
            resp->Data[i] = (UCHAR)((dword >> ((off & 3) * 8)) & 0xFF);
            ++count;
        }

        resp->Length = count;
        status = STATUS_SUCCESS;
        info = count + sizeof(ULONG);
    }

done:
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING devName;
    UNICODE_STRING dosName;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = PciCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = PciCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PciReadDispatch;
    DriverObject->DriverUnload = PciDriverUnload;

    RtlInitUnicodeString(&devName, PCI_DRV_NAME);
    status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
    if (!NT_SUCCESS(status))
        return status;

    RtlInitUnicodeString(&dosName, PCI_DRV_DOS);
    status = IoCreateSymbolicLink(&dosName, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    return STATUS_SUCCESS;
}