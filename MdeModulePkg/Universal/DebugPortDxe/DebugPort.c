/** @file
  Top level C file for debugport driver.  Contains initialization function.
  This driver layers on top of SerialIo.
  ALL CODE IN THE SERIALIO STACK MUST BE RE-ENTRANT AND CALLABLE FROM
  INTERRUPT CONTEXT

Copyright (c) 2006 - 2008, Intel Corporation. <BR>
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/


#include "DebugPort.h"

//
// Misc. functions local to this module..
//
VOID
GetDebugPortVariable (
  DEBUGPORT_DEVICE            *DebugPortDevice
  )
/*++

Routine Description:
  Local worker function to obtain device path information from DebugPort variable.
  Records requested settings in DebugPort device structure.

Arguments:
  DEBUGPORT_DEVICE  *DebugPortDevice,

Returns:

  Nothing

--*/
{
  UINTN                     DataSize;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_STATUS                Status;

  DataSize = 0;

  Status = gRT->GetVariable (
                  (CHAR16 *) EFI_DEBUGPORT_VARIABLE_NAME,
                  &gEfiDebugPortVariableGuid,
                  NULL,
                  &DataSize,
                  DebugPortDevice->DebugPortVariable
                  );

  if (Status == EFI_BUFFER_TOO_SMALL) {
    if (gDebugPortDevice->DebugPortVariable != NULL) {
      FreePool (gDebugPortDevice->DebugPortVariable);
    }

    DebugPortDevice->DebugPortVariable = AllocatePool (DataSize);
    if (DebugPortDevice->DebugPortVariable != NULL) {
      gRT->GetVariable (
            (CHAR16 *) EFI_DEBUGPORT_VARIABLE_NAME,
            &gEfiDebugPortVariableGuid,
            NULL,
            &DataSize,
            DebugPortDevice->DebugPortVariable
            );
      DevicePath = (EFI_DEVICE_PATH_PROTOCOL *) DebugPortDevice->DebugPortVariable;
      while (!EfiIsDevicePathEnd (DevicePath) && !EfiIsUartDevicePath (DevicePath)) {
        DevicePath = EfiNextDevicePathNode (DevicePath);
      }

      if (EfiIsDevicePathEnd (DevicePath)) {
        FreePool (gDebugPortDevice->DebugPortVariable);
        DebugPortDevice->DebugPortVariable = NULL;
      } else {
        CopyMem (
          &DebugPortDevice->BaudRate,
          &((UART_DEVICE_PATH *) DevicePath)->BaudRate,
          sizeof (((UART_DEVICE_PATH *) DevicePath)->BaudRate)
          );
        DebugPortDevice->ReceiveFifoDepth = DEBUGPORT_UART_DEFAULT_FIFO_DEPTH;
        DebugPortDevice->Timeout          = DEBUGPORT_UART_DEFAULT_TIMEOUT;
        CopyMem (
          &DebugPortDevice->Parity,
          &((UART_DEVICE_PATH *) DevicePath)->Parity,
          sizeof (((UART_DEVICE_PATH *) DevicePath)->Parity)
          );
        CopyMem (
          &DebugPortDevice->DataBits,
          &((UART_DEVICE_PATH *) DevicePath)->DataBits,
          sizeof (((UART_DEVICE_PATH *) DevicePath)->DataBits)
          );
        CopyMem (
          &DebugPortDevice->StopBits,
          &((UART_DEVICE_PATH *) DevicePath)->StopBits,
          sizeof (((UART_DEVICE_PATH *) DevicePath)->StopBits)
          );
      }
    }
  }
}

//
// Globals
//

EFI_DRIVER_BINDING_PROTOCOL gDebugPortDriverBinding = {
  DebugPortSupported,
  DebugPortStart,
  DebugPortStop,
  DEBUGPORT_DRIVER_VERSION,
  NULL,
  NULL
};

DEBUGPORT_DEVICE  *gDebugPortDevice;

//
// implementation code
//

EFI_STATUS
EFIAPI
InitializeDebugPortDriver (
  IN EFI_HANDLE             ImageHandle,
  IN EFI_SYSTEM_TABLE       *SystemTable
  )
/*++

Routine Description:
  Driver entry point.  Reads DebugPort variable to determine what device and settings
  to use as the debug port.  Binds exclusively to SerialIo. Reverts to defaults \
  if no variable is found.

  Creates debugport and devicepath protocols on new handle.

Arguments:
  ImageHandle,
  SystemTable

Returns:

  EFI_UNSUPPORTED
  EFI_OUT_OF_RESOURCES

--*/
{
  EFI_STATUS    Status;

  //
  // Install driver model protocol(s).
  //
  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gDebugPortDriverBinding,
             ImageHandle,
             &gDebugPortComponentName,
             &gDebugPortComponentName2
             );
  ASSERT_EFI_ERROR (Status);
  //
  // Allocate and Initialize dev structure
  //
  gDebugPortDevice = AllocateZeroPool (sizeof (DEBUGPORT_DEVICE));
  if (gDebugPortDevice == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  //
  // Fill in static and default pieces of device structure first.
  //
  gDebugPortDevice->Signature = DEBUGPORT_DEVICE_SIGNATURE;

  gDebugPortDevice->DebugPortInterface.Reset = DebugPortReset;
  gDebugPortDevice->DebugPortInterface.Read = DebugPortRead;
  gDebugPortDevice->DebugPortInterface.Write = DebugPortWrite;
  gDebugPortDevice->DebugPortInterface.Poll = DebugPortPoll;

  gDebugPortDevice->BaudRate = DEBUGPORT_UART_DEFAULT_BAUDRATE;
  gDebugPortDevice->ReceiveFifoDepth = DEBUGPORT_UART_DEFAULT_FIFO_DEPTH;
  gDebugPortDevice->Timeout = DEBUGPORT_UART_DEFAULT_TIMEOUT;
  gDebugPortDevice->Parity = (EFI_PARITY_TYPE) DEBUGPORT_UART_DEFAULT_PARITY;
  gDebugPortDevice->DataBits = DEBUGPORT_UART_DEFAULT_DATA_BITS;
  gDebugPortDevice->StopBits = (EFI_STOP_BITS_TYPE) DEBUGPORT_UART_DEFAULT_STOP_BITS;

  return EFI_SUCCESS;
}
//
// DebugPort driver binding member functions...
//
EFI_STATUS
EFIAPI
DebugPortSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
/*++

Routine Description:
  Checks to see that there's not already a DebugPort interface somewhere.  If so,
  fail.

  If there's a DEBUGPORT variable, the device path must match exactly.  If there's
  no DEBUGPORT variable, then device path is not checked and does not matter.

  Checks to see that there's a serial io interface on the controller handle
  that can be bound BY_DRIVER | EXCLUSIVE.

  If all these tests succeed, then we return EFI_SUCCESS, else, EFI_UNSUPPORTED
  or other error returned by OpenProtocol.

Arguments:
  This
  ControllerHandle
  RemainingDevicePath

Returns:
  EFI_UNSUPPORTED
  EFI_OUT_OF_RESOURCES
  EFI_SUCCESS

--*/
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *Dp1;
  EFI_DEVICE_PATH_PROTOCOL  *Dp2;
  EFI_SERIAL_IO_PROTOCOL    *SerialIo;
  EFI_DEBUGPORT_PROTOCOL    *DebugPortInterface;
  EFI_HANDLE                TempHandle;

  //
  // Check to see that there's not a debugport protocol already published
  //
  if (gBS->LocateProtocol (&gEfiDebugPortProtocolGuid, NULL, (VOID **) &DebugPortInterface) != EFI_NOT_FOUND) {
    return EFI_UNSUPPORTED;
  }
  //
  // Read DebugPort variable to determine debug port selection and parameters
  //
  GetDebugPortVariable (gDebugPortDevice);

  if (gDebugPortDevice->DebugPortVariable != NULL) {
    //
    // There's a DEBUGPORT variable, so do LocateDevicePath and check to see if
    // the closest matching handle matches the controller handle, and if it does,
    // check to see that the remaining device path has the DebugPort GUIDed messaging
    // device path only.  Otherwise, it's a mismatch and EFI_UNSUPPORTED is returned.
    //
    Dp1 = DuplicateDevicePath ((EFI_DEVICE_PATH_PROTOCOL *) gDebugPortDevice->DebugPortVariable);
    if (Dp1 == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Dp2 = Dp1;

    Status = gBS->LocateDevicePath (
                    &gEfiSerialIoProtocolGuid,
                    &Dp2,
                    &TempHandle
                    );

    if (Status == EFI_SUCCESS && TempHandle != ControllerHandle) {
      Status = EFI_UNSUPPORTED;
    }

    if (Status == EFI_SUCCESS && (Dp2->Type != 3 || Dp2->SubType != 10 || *((UINT16 *) Dp2->Length) != 20)) {
      Status = EFI_UNSUPPORTED;
    }

    if (Status == EFI_SUCCESS && CompareMem (&gEfiDebugPortDevicePathGuid, Dp2 + 1, sizeof (EFI_GUID))) {
      Status = EFI_UNSUPPORTED;
    }

    FreePool (Dp1);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSerialIoProtocolGuid,
                  (VOID **) &SerialIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER | EFI_OPEN_PROTOCOL_EXCLUSIVE
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
        ControllerHandle,
        &gEfiSerialIoProtocolGuid,
        This->DriverBindingHandle,
        ControllerHandle
        );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DebugPortStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
/*++

Routine Description:
  Binds exclusively to serial io on the controller handle.  Produces DebugPort
  protocol and DevicePath on new handle.

Arguments:
  This
  ControllerHandle
  RemainingDevicePath

Returns:
  EFI_OUT_OF_RESOURCES
  EFI_SUCCESS
--*/
{
  EFI_STATUS                Status;
  DEBUGPORT_DEVICE_PATH     DebugPortDP;
  EFI_DEVICE_PATH_PROTOCOL  EndDP;
  EFI_DEVICE_PATH_PROTOCOL  *Dp1;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSerialIoProtocolGuid,
                  (VOID **) &gDebugPortDevice->SerialIoBinding,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER | EFI_OPEN_PROTOCOL_EXCLUSIVE
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gDebugPortDevice->SerialIoDeviceHandle = ControllerHandle;

  //
  // Initialize the Serial Io interface...
  //
  Status = gDebugPortDevice->SerialIoBinding->SetAttributes (
                                                gDebugPortDevice->SerialIoBinding,
                                                gDebugPortDevice->BaudRate,
                                                gDebugPortDevice->ReceiveFifoDepth,
                                                gDebugPortDevice->Timeout,
                                                gDebugPortDevice->Parity,
                                                gDebugPortDevice->DataBits,
                                                gDebugPortDevice->StopBits
                                                );
  if (EFI_ERROR (Status)) {
    gDebugPortDevice->BaudRate          = 0;
    gDebugPortDevice->Parity            = DefaultParity;
    gDebugPortDevice->DataBits          = 0;
    gDebugPortDevice->StopBits          = DefaultStopBits;
    gDebugPortDevice->ReceiveFifoDepth  = 0;
    Status = gDebugPortDevice->SerialIoBinding->SetAttributes (
                                                  gDebugPortDevice->SerialIoBinding,
                                                  gDebugPortDevice->BaudRate,
                                                  gDebugPortDevice->ReceiveFifoDepth,
                                                  gDebugPortDevice->Timeout,
                                                  gDebugPortDevice->Parity,
                                                  gDebugPortDevice->DataBits,
                                                  gDebugPortDevice->StopBits
                                                  );
    if (EFI_ERROR (Status)) {
      gBS->CloseProtocol (
            ControllerHandle,
            &gEfiSerialIoProtocolGuid,
            This->DriverBindingHandle,
            ControllerHandle
            );
      return Status;
    }
  }

  gDebugPortDevice->SerialIoBinding->Reset (gDebugPortDevice->SerialIoBinding);

  //
  // Create device path instance for DebugPort
  //
  DebugPortDP.Header.Type     = MESSAGING_DEVICE_PATH;
  DebugPortDP.Header.SubType  = MSG_VENDOR_DP;
  SetDevicePathNodeLength (&(DebugPortDP.Header), sizeof (DebugPortDP));
  CopyMem (&DebugPortDP.Guid, &gEfiDebugPortDevicePathGuid, sizeof (EFI_GUID));

  Dp1 = DevicePathFromHandle (ControllerHandle);
  if (Dp1 == NULL) {
    Dp1 = &EndDP;
    SetDevicePathEndNode (Dp1);
  }

  gDebugPortDevice->DebugPortDevicePath = AppendDevicePathNode (Dp1, (EFI_DEVICE_PATH_PROTOCOL *) &DebugPortDP);
  if (gDebugPortDevice->DebugPortDevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  //
  // Publish DebugPort and Device Path protocols
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gDebugPortDevice->DebugPortDeviceHandle,
                  &gEfiDevicePathProtocolGuid,
                  gDebugPortDevice->DebugPortDevicePath,
                  &gEfiDebugPortProtocolGuid,
                  &gDebugPortDevice->DebugPortInterface,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    gBS->CloseProtocol (
          ControllerHandle,
          &gEfiSerialIoProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );
    return Status;
  }
  //
  // Connect debugport child to serial io
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSerialIoProtocolGuid,
                  (VOID **) &gDebugPortDevice->SerialIoBinding,
                  This->DriverBindingHandle,
                  gDebugPortDevice->DebugPortDeviceHandle,
                  EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  );

  if (EFI_ERROR (Status)) {
    DEBUG_CODE_BEGIN ();
      UINTN  BufferSize;

      BufferSize = 48;
      DebugPortWrite (
        &gDebugPortDevice->DebugPortInterface,
        0,
        &BufferSize,
        "DebugPort driver failed to open child controller\n\n"
        );
    DEBUG_CODE_END ();

    gBS->CloseProtocol (
          ControllerHandle,
          &gEfiSerialIoProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );
    return Status;
  }

  DEBUG_CODE_BEGIN ();
    UINTN  BufferSize;

    BufferSize = 38;
    DebugPortWrite (
      &gDebugPortDevice->DebugPortInterface,
      0,
      &BufferSize,
      "Hello World from the DebugPort driver\n\n"
      );
  DEBUG_CODE_END ();

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DebugPortStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN  EFI_HANDLE                     ControllerHandle,
  IN  UINTN                          NumberOfChildren,
  IN  EFI_HANDLE                     *ChildHandleBuffer
  )
/*++

Routine Description:
  We're never intending to be stopped via the driver model so this just returns
  EFI_UNSUPPORTED

Arguments:
  Per UEFI 2.0 driver model

Returns:
  EFI_UNSUPPORTED
  EFI_SUCCESS

--*/
{
  EFI_STATUS  Status;

  if (NumberOfChildren == 0) {
    //
    // Close the bus driver
    //
    gBS->CloseProtocol (
          ControllerHandle,
          &gEfiSerialIoProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );

    gDebugPortDevice->SerialIoBinding = NULL;

    gBS->CloseProtocol (
          ControllerHandle,
          &gEfiDevicePathProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );

    FreePool (gDebugPortDevice->DebugPortDevicePath);

    return EFI_SUCCESS;
  } else {
    //
    // Disconnect SerialIo child handle
    //
    Status = gBS->CloseProtocol (
                    gDebugPortDevice->SerialIoDeviceHandle,
                    &gEfiSerialIoProtocolGuid,
                    This->DriverBindingHandle,
                    gDebugPortDevice->DebugPortDeviceHandle
                    );

    if (EFI_ERROR (Status)) {
      return Status;
    }
    //
    // Unpublish our protocols (DevicePath, DebugPort)
    //
    Status = gBS->UninstallMultipleProtocolInterfaces (
                    gDebugPortDevice->DebugPortDeviceHandle,
                    &gEfiDevicePathProtocolGuid,
                    gDebugPortDevice->DebugPortDevicePath,
                    &gEfiDebugPortProtocolGuid,
                    &gDebugPortDevice->DebugPortInterface,
                    NULL
                    );

    if (EFI_ERROR (Status)) {
      gBS->OpenProtocol (
            ControllerHandle,
            &gEfiSerialIoProtocolGuid,
            (VOID **) &gDebugPortDevice->SerialIoBinding,
            This->DriverBindingHandle,
            gDebugPortDevice->DebugPortDeviceHandle,
            EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
            );
    } else {
      gDebugPortDevice->DebugPortDeviceHandle = NULL;
    }
  }

  return Status;
}
//
// Debugport protocol member functions
//
EFI_STATUS
EFIAPI
DebugPortReset (
  IN EFI_DEBUGPORT_PROTOCOL   *This
  )
/*++

Routine Description:
  DebugPort protocol member function.  Calls SerialIo:GetControl to flush buffer.
  We cannot call SerialIo:SetAttributes because it uses pool services, which use
  locks, which affect TPL, so it's not interrupt context safe or re-entrant.
  SerialIo:Reset() calls SetAttributes, so it can't be used either.

  The port itself should be fine since it was set up during initialization.

Arguments:
  This

Returns:

  EFI_SUCCESS

--*/
{
  UINTN             BufferSize;
  UINTN             BitBucket;

  while (This->Poll (This) == EFI_SUCCESS) {
    BufferSize = 1;
    This->Read (This, 0, &BufferSize, &BitBucket);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DebugPortRead (
  IN EFI_DEBUGPORT_PROTOCOL   *This,
  IN UINT32                   Timeout,
  IN OUT UINTN                *BufferSize,
  IN VOID                     *Buffer
  )
/*++

Routine Description:
  DebugPort protocol member function.  Calls SerialIo:Read() after setting
  if it's different than the last SerialIo access.

Arguments:
  IN EFI_DEBUGPORT_PROTOCOL   *This
  IN UINT32                   Timeout,
  IN OUT UINTN                *BufferSize,
  IN VOID                     *Buffer

Returns:

  EFI_STATUS

--*/
{
  DEBUGPORT_DEVICE  *DebugPortDevice;
  UINTN             LocalBufferSize;
  EFI_STATUS        Status;
  UINT8             *BufferPtr;

  DebugPortDevice = DEBUGPORT_DEVICE_FROM_THIS (This);
  BufferPtr       = Buffer;
  LocalBufferSize = *BufferSize;
  do {
    Status = DebugPortDevice->SerialIoBinding->Read (
                                                DebugPortDevice->SerialIoBinding,
                                                &LocalBufferSize,
                                                BufferPtr
                                                );
    if (Status == EFI_TIMEOUT) {
      if (Timeout > DEBUGPORT_UART_DEFAULT_TIMEOUT) {
        Timeout -= DEBUGPORT_UART_DEFAULT_TIMEOUT;
      } else {
        Timeout = 0;
      }
    } else if (EFI_ERROR (Status)) {
      break;
    }

    BufferPtr += LocalBufferSize;
    LocalBufferSize = *BufferSize - (BufferPtr - (UINT8 *) Buffer);
  } while (LocalBufferSize != 0 && Timeout > 0);

  *BufferSize = (UINTN) (BufferPtr - (UINT8 *) Buffer);

  return Status;
}

EFI_STATUS
EFIAPI
DebugPortWrite (
  IN EFI_DEBUGPORT_PROTOCOL   *This,
  IN UINT32                   Timeout,
  IN OUT UINTN                *BufferSize,
  OUT VOID                    *Buffer
  )
/*++

Routine Description:
  DebugPort protocol member function.  Calls SerialIo:Write() Writes 8 bytes at
  a time and does a GetControl between 8 byte writes to help insure reads are
  interspersed This is poor-man's flow control..

Arguments:
  This               - Pointer to DebugPort protocol
  Timeout            - Timeout value
  BufferSize         - On input, the size of Buffer.
                       On output, the amount of data actually written.
  Buffer             - Pointer to buffer to write

Returns:
  EFI_SUCCESS        - The data was written.
  EFI_DEVICE_ERROR   - The device reported an error.
  EFI_TIMEOUT        - The data write was stopped due to a timeout.

--*/
{
  DEBUGPORT_DEVICE  *DebugPortDevice;
  UINTN             Position;
  UINTN             WriteSize;
  EFI_STATUS        Status;
  UINT32            SerialControl;

  Status          = EFI_SUCCESS;
  DebugPortDevice = DEBUGPORT_DEVICE_FROM_THIS (This);

  WriteSize       = 8;
  for (Position = 0; Position < *BufferSize && !EFI_ERROR (Status); Position += WriteSize) {
    DebugPortDevice->SerialIoBinding->GetControl (
                                        DebugPortDevice->SerialIoBinding,
                                        &SerialControl
                                        );
    if (*BufferSize - Position < 8) {
      WriteSize = *BufferSize - Position;
    }

    Status = DebugPortDevice->SerialIoBinding->Write (
                                                DebugPortDevice->SerialIoBinding,
                                                &WriteSize,
                                                &((UINT8 *) Buffer)[Position]
                                                );
  }

  *BufferSize = Position;
  return Status;
}

EFI_STATUS
EFIAPI
DebugPortPoll (
  IN EFI_DEBUGPORT_PROTOCOL   *This
  )
/*++

Routine Description:
  DebugPort protocol member function.  Calls SerialIo:Write() after setting
  if it's different than the last SerialIo access.

Arguments:
  IN EFI_DEBUGPORT_PROTOCOL   *This

Returns:
  EFI_SUCCESS - At least 1 character is ready to be read from the DebugPort interface
  EFI_NOT_READY - There are no characters ready to read from the DebugPort interface
  EFI_DEVICE_ERROR - A hardware failure occured... (from SerialIo)

--*/
{
  EFI_STATUS        Status;
  UINT32            SerialControl;
  DEBUGPORT_DEVICE  *DebugPortDevice;

  DebugPortDevice = DEBUGPORT_DEVICE_FROM_THIS (This);

  Status = DebugPortDevice->SerialIoBinding->GetControl (
                                              DebugPortDevice->SerialIoBinding,
                                              &SerialControl
                                              );

  if (!EFI_ERROR (Status)) {
    if (SerialControl & EFI_SERIAL_INPUT_BUFFER_EMPTY) {
      Status = EFI_NOT_READY;
    } else {
      Status = EFI_SUCCESS;
    }
  }

  return Status;
}

EFI_STATUS
EFIAPI
ImageUnloadHandler (
  EFI_HANDLE ImageHandle
  )
/*++

Routine Description:
  Unload function that is registered in the LoadImage protocol.  It un-installs
  protocols produced and deallocates pool used by the driver.  Called by the core
  when unloading the driver.

Arguments:
  EFI_HANDLE ImageHandle

Returns:

  EFI_SUCCESS

--*/
{
  EFI_STATUS  Status;

  if (gDebugPortDevice->SerialIoBinding != NULL) {
    return EFI_ABORTED;
  }

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  ImageHandle,
                  &gEfiDriverBindingProtocolGuid,
                  &gDebugPortDevice->DriverBindingInterface,
                  &gEfiComponentNameProtocolGuid,
                  &gDebugPortDevice->ComponentNameInterface,
                  NULL
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Clean up allocations
  //
  if (gDebugPortDevice->DebugPortVariable != NULL) {
    FreePool (gDebugPortDevice->DebugPortVariable);
  }

  FreePool (gDebugPortDevice);

  return EFI_SUCCESS;
}
