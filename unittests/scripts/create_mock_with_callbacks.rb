# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
# Wrapper around CMock that enables the :callback plugin so that
# StubWithCallback / AddCallback / Stub are generated for every mock function.
# Used by generate_mocks.sh in place of CMock/scripts/create_mock.rb.

require "#{ENV['CMOCK_DIR']}/lib/cmock"

raise 'Header file to mock must be specified!' unless ARGV.length >= 1

mock_out    = ENV.fetch('MOCK_OUT', './build/test/mocks')
mock_prefix = ENV.fetch('MOCK_PREFIX', 'mock_')
cmock = CMock.new(
  plugins: %i[ignore return_thru_ptr callback],
  mock_prefix: mock_prefix,
  mock_path: mock_out
)
cmock.setup_mocks(ARGV[0])
