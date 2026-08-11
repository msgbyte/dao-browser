// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {loadTimeData} from '//resources/js/load_time_data.js';

{
  const trustedWindow = window as unknown as {
    trustedTypes?: {createPolicy: (name: string, rules: object) => void};
  };
  trustedWindow.trustedTypes?.createPolicy('default', {
    createHTML: (value: string) => value,
    createScript: (value: string) => value,
    createScriptURL: (value: string) => value,
  });
}

document.title = loadTimeData.getString('daoImportPageTitle');

import './dao_import_app.js';
