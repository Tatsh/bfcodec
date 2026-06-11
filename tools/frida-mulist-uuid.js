'use strict';

// Recover the per-device Keychain UUID that REFLEC BEAT plus uses to derive the
// mulist/prodlist Blowfish key.
//
// Background (from static analysis of -[AppDelegate musicListKey] @ 0x554e8):
//   key = MD5(UUID),  IV = E36631DA2C85A064
// The UUID is stored in the iOS Keychain as a Generic Password with
//   kSecAttrService = the app bundle identifier
//   kSecAttrAccount = "ApplicationUniqueID"
// and decoded as a UTF-8 NSString. The canonical CFUUID form is uppercase.
//
// This script MUST run inside the app process so the Keychain access group and
// entitlements match the stored item. Run it with:
//   frida -U -f <bundle id> -l tools/frida-mulist-uuid.js   (spawn)
//   frida -U -n 'REFLEC BEAT plus' -l tools/frida-mulist-uuid.js   (attach)
// The device must have been unlocked at least once since boot (the item is
// kSecAttrAccessibleAfterFirstUnlock).

const ACCOUNT = 'ApplicationUniqueID';
const KNOWN_IV = 'E36631DA2C85A064';

// Frida 17 removed Module.findExportByName(null, ...). Resolve a symbol
// globally, falling back to the frameworks that export these symbols.
function exp(name) {
  let p = Module.findGlobalExportByName(name);
  if (p === null) {
    const mods = ['Security', 'libcommonCrypto.dylib', 'libSystem.B.dylib', 'CoreFoundation'];
    for (let i = 0; i < mods.length; i++) {
      try {
        const e = Process.getModuleByName(mods[i]).findExportByName(name);
        if (e !== null) {
          p = e;
          break;
        }
      } catch (err) {
        // module not loaded; keep trying
      }
    }
  }
  return p;
}

// Resolve a Security.framework / CoreFoundation CFStringRef constant by name.
// These are exported data symbols whose value is a pointer to the CFStringRef.
function cfConst(moduleName, symbol) {
  const p = exp(symbol);
  if (p === null) {
    throw new Error(`Could not resolve ${symbol}`);
  }
  return new ObjC.Object(p.readPointer());
}

// Compute MD5(bytes) and return it as a 32-char lowercase hex string, using the
// device's own CommonCrypto so the result matches the app exactly.
function md5Hex(dataPtr, byteLength) {
  const ccMd5Ptr = exp('CC_MD5');
  const ccMd5 = new NativeFunction(ccMd5Ptr, 'pointer', ['pointer', 'uint32', 'pointer']);
  const digest = Memory.alloc(16);
  ccMd5(dataPtr, byteLength, digest);
  const bytes = new Uint8Array(digest.readByteArray(16));
  let hex = '';
  for (let i = 0; i < bytes.length; i++) {
    hex += `0${bytes[i].toString(16)}`.slice(-2);
  }
  return hex;
}

let reported = false;

function report(uuid, source) {
  if (uuid === null || uuid === undefined || uuid.length === 0) {
    return;
  }
  const trimmed = uuid.trim();
  const utf8 = Memory.allocUtf8String(trimmed);
  const key = md5Hex(utf8, trimmed.length);
  console.log('');
  console.log('==================== mulist KEY MATERIAL ====================');
  console.log(`source        : ${source}`);
  console.log(`Keychain UUID : ${trimmed}`);
  console.log(`MD5 key (hex) : ${key}`);
  console.log(`IV (known)    : ${KNOWN_IV}`);
  console.log(`decrypt with  : unbfc --uuid ${trimmed} mulist`);
  console.log('============================================================');
  console.log('');
  reported = true;
}

// Part A: actively read the stored UUID straight out of the Keychain. This does
// not require waiting for the app to touch mulist.
function pullFromKeychain() {
  const NSBundle = ObjC.classes.NSBundle;
  const NSString = ObjC.classes.NSString;
  const NSNumber = ObjC.classes.NSNumber;
  const NSMutableDictionary = ObjC.classes.NSMutableDictionary;

  const secItemCopyMatchingPtr = exp('SecItemCopyMatching');
  const secItemCopyMatching = new NativeFunction(secItemCopyMatchingPtr, 'int', [
    'pointer',
    'pointer',
  ]);

  const bundleId = NSBundle.mainBundle().bundleIdentifier();
  console.log(`[*] bundle identifier: ${bundleId}`);

  // Try the exact query first (service + account), then progressively looser
  // queries so a renamed service or access group still surfaces the item.
  const attempts = [
    { withService: true, label: 'service + account' },
    { withService: false, label: 'account only' },
  ];

  for (let i = 0; i < attempts.length; i++) {
    const query = NSMutableDictionary.dictionary();
    query.setObject_forKey_(
      cfConst('Security', 'kSecClassGenericPassword'),
      cfConst('Security', 'kSecClass'),
    );
    query.setObject_forKey_(
      NSString.stringWithString_(ACCOUNT),
      cfConst('Security', 'kSecAttrAccount'),
    );
    if (attempts[i].withService) {
      query.setObject_forKey_(bundleId, cfConst('Security', 'kSecAttrService'));
    }
    query.setObject_forKey_(
      cfConst('Security', 'kSecMatchLimitOne'),
      cfConst('Security', 'kSecMatchLimit'),
    );
    query.setObject_forKey_(NSNumber.numberWithBool_(1), cfConst('Security', 'kSecReturnData'));

    const out = Memory.alloc(Process.pointerSize);
    const status = secItemCopyMatching(query.handle, out);
    if (status === 0) {
      const data = new ObjC.Object(out.readPointer()); // NSData (UTF-8 bytes)
      const uuid = NSString.alloc().initWithData_encoding_(data, 4).toString();
      report(uuid, `Keychain direct pull (${attempts[i].label})`);
      return;
    }
    console.log(`[!] SecItemCopyMatching (${attempts[i].label}) -> status ${status}`);
  }
  console.log('[!] Not found via direct pull. The passive hooks below will catch');
  console.log('    the UUID the next time the app keys mulist/prodlist.');
}

// Part B: passive hooks, in case the direct pull is blocked (e.g. access group)
// or you would rather observe the live derivation.
function installHooks() {
  // B1: the exact getter. Its return value IS the UUID string.
  try {
    const resolver = new ApiResolver('objc');
    const matches = resolver.enumerateMatches('-[* musicListKey]');
    matches.forEach(function (m) {
      Interceptor.attach(m.address, {
        onLeave: function (retval) {
          try {
            report(new ObjC.Object(retval).toString(), m.name);
          } catch (e) {
            // ignore non-object returns
          }
        },
      });
    });
    console.log(`[*] hooked ${matches.length} musicListKey implementation(s)`);
  } catch (e) {
    console.log(`[!] musicListKey hook failed: ${e}`);
  }

  // B2: catch-all on the key-derivation primitive. The app MD5s the UUID string
  // (36-char canonical form) to build the Blowfish key.
  try {
    const ccMd5Ptr = exp('CC_MD5');
    Interceptor.attach(ccMd5Ptr, {
      onEnter: function (args) {
        this.data = args[0];
        this.len = args[1].toInt32();
      },
      onLeave: function () {
        if (this.len !== 36) {
          return;
        }
        try {
          const s = this.data.readUtf8String(36);
          if (
            /^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/.test(s)
          ) {
            report(s, 'CC_MD5 input (36-byte UUID)');
          }
        } catch (e) {
          // unreadable input, ignore
        }
      },
    });
    console.log('[*] hooked CC_MD5');
  } catch (e) {
    console.log(`[!] CC_MD5 hook failed: ${e}`);
  }
}

if (!ObjC.available) {
  console.log('[!] Objective-C runtime not available; is this the app process?');
} else {
  ObjC.schedule(ObjC.mainQueue, function () {
    try {
      installHooks();
      pullFromKeychain();
      if (!reported) {
        console.log('[*] Waiting on hooks. Open the app/menu that loads the music');
        console.log('    list (mulist) to trigger the key derivation.');
      }
    } catch (e) {
      console.log(`[!] error: ${e}\n${e.stack}`);
    }
  });
}
