// THIS FILE WAS MODIFIED BY Henry Strickland (github: strickyak)
// IN THE FOLLOWING WAY:
//   The package name was chanaged from `serial` to `main`.
//   Rename type OpenOptions to OpenSerialOptions.
// See serial.LICENSE

// Copyright 2011 Aaron Jacobs. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package main

import "io"

func openInternal(options OpenSerialOptions) (io.ReadWriteCloser, error) {
	return nil, "Not implemented on this OS."
}
