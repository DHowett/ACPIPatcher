#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/AcpiSystemDescriptionTable.h>

#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>

#include "FsHelpers.h"

EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER        *gRsdp      = NULL;
EFI_ACPI_SDT_HEADER                                 *gXsdt      = NULL;
EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE           *gFacp      = NULL;
UINT64                                              gXsdtEnd    = 0;

EFI_STATUS
PatchAcpi(EFI_FILE_PROTOCOL* Directory)
{
    UINTN                BufferSize     = 0;
    UINTN                ReadSize       = 0;
    EFI_STATUS           Status         = EFI_SUCCESS;
    EFI_FILE_INFO*       FileInfo       = NULL;
    EFI_FILE_PROTOCOL*   FileProtocol   = NULL;
    VOID*                FileBuffer     = NULL;
    
    BufferSize = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 512;
    Status = gBS -> AllocatePool( EfiBootServicesCode, BufferSize, (VOID**)&FileInfo);
    while(1){
        ReadSize = BufferSize;
        Status = Directory -> Read(Directory, &ReadSize, FileInfo);
        if (Status != EFI_SUCCESS) {
            Print(L"ListDirectory Error: %r\n", Status);
            return 1;
        }
        
        if(ReadSize == 0) break;
        if(StrnCmp(&FileInfo->FileName[0], L".", 1) &&
           StrnCmp(&FileInfo->FileName[0], L"_", 1) &&
           StrStr(FileInfo->FileName, L".aml")) {
            
            Print(L"Reading file: %s\n", FileInfo->FileName);
            Status = FsOpenFile(Directory, FileInfo->FileName, &FileProtocol);
            if (Status != EFI_SUCCESS) {
                Print(L"ListDirectory::Open(%s)\nError: %r\n", FileInfo->FileName, Status);
                return EFI_LOAD_ERROR;
            }
            
            BufferSize = FileInfo->FileSize;
            Status = FsReadFileToBuffer(FileProtocol, BufferSize, &FileBuffer);
            if(Status != EFI_SUCCESS) {
                Print(L"ListDirectory::Read(%s)\nError: %r\n", FileInfo->FileName, Status);
                return Status;
            }
            
            if(!StrnCmp(FileInfo->FileName, L"DSDT.aml", 9)) {
                gFacp->Dsdt = (UINT32)FileBuffer;
                Print(L"  New DSDT address: 0x%x\n", gFacp->Dsdt);
                continue;
            }
            
            ((UINT64 *)gXsdtEnd)[0] = (UINT64)FileBuffer;
            gXsdt->Length = gXsdt->Length + sizeof(UINT64);
            gXsdtEnd =  gRsdp->XsdtAddress + gXsdt->Length;
            Print(L"  Added successfully from: 0x%x\n", (UINT64)FileBuffer);
        }
        
        
    }
    Status = gBS->FreePool(FileInfo);
    return Status;
}

EFI_STATUS
FindFacp()
{
    EFI_ACPI_SDT_HEADER *Entry;
    UINT32 EntryCount = (gXsdt->Length - sizeof (EFI_ACPI_SDT_HEADER)) / sizeof(UINT64);
    UINT64 *EntryPtr = (UINT64 *)(gXsdt + 1);
    for (UINTN Index = 0; Index < EntryCount; Index++, EntryPtr++) {
        Entry = (EFI_ACPI_SDT_HEADER *)((UINTN)(*EntryPtr));
        if(Entry->Signature == EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE)
            gFacp = (EFI_ACPI_2_0_FIXED_ACPI_DESCRIPTION_TABLE *)Entry;
        
    }
    
    return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
AcpiPatcherEntryPoint (
                       IN EFI_HANDLE        ImageHandle,
                       IN EFI_SYSTEM_TABLE  *SystemTable
                       )
{
    EFI_STATUS                 Status          = EFI_SUCCESS;
    EFI_FILE_PROTOCOL       *AcpiFolder     = NULL;
    EFI_FILE_PROTOCOL       *SelfDir        = NULL;
    
    
    EfiGetSystemConfigurationTable(&gEfiAcpi20TableGuid, (VOID **)&gRsdp);
    if (!gRsdp) {
        Print(L"ERROR: Could not find a RSDP.\n");
        return 1;
    }
    
    gXsdt = (EFI_ACPI_SDT_HEADER *)(gRsdp->XsdtAddress);
    Print(L"Found XSDT of size 0x%x (%u) at 0x%p\n", gXsdt->Length, gXsdt->Length, gXsdt);
    gXsdtEnd =  gRsdp->XsdtAddress + gXsdt->Length;
    
    FindFacp();
    
    UINT32 EntryCount = (gXsdt->Length - sizeof (EFI_ACPI_SDT_HEADER)) / sizeof(UINT64);
    Print(L"XSDT Entry Count: %u\n", EntryCount);
    
    SelfDir = FsGetSelfDir();
    if(!SelfDir) {
        Print(L"Error: Could not find current working directory\n");
        return 1;
    }
    
    Status = SelfDir->Open(
                           SelfDir,
                           &AcpiFolder,
                           L"ACPI",
                           EFI_FILE_MODE_READ,
                           EFI_FILE_READ_ONLY | EFI_FILE_HIDDEN | EFI_FILE_SYSTEM);
    
    if (Status != EFI_SUCCESS) {
        Print(L"Could not open ACPI Folder: %r\n", Status);
        return Status;
    }
    
    Print(L"Patching ACPI\n");
    Status = PatchAcpi(AcpiFolder);
    if(Status != EFI_SUCCESS) {
        Print(L"Could not patch ACPI\n");
    } else {
        Print(L"ACPI patching successful\n");
    }
    return Status;
}
