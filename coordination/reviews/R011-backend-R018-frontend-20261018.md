# Review: Backend R011 and Frontend R018

Date: 2026-10-18 Asia/Hong_Kong. Candidate `c97e219`.

R011 is accepted as a runtime+static-confirmed attribution. Exact 22F82
libxpc cache identity, image range and entry bytes match the crash PC; the
installed libmachook callsite is confirmed as `blraaz x19`; `MSFindSymbol`
returns an unsigned raw address; all three crash reports show raw x19 at the
valid libxpc entry, poisoned FAR, instruction-abort ESR and the same caller LR.
The callee entry takes x0 and is not reached, so this failure is at the
function-pointer authentication boundary. The 22F82 callee ABI is consistent
with the one-x0 source declaration, while dictionary semantics remain
unvalidated.

R018 independently confirms the project/toolchain contract: C function
pointers use A-key with zero discriminator, and existing provider/AppInput
code signs raw function pointers with `ptrauth_sign_unauthenticated(...,
ptrauth_key_asia/function_pointer, 0)`. It also correctly leaves unrelated
Steam/Metal raw callsites unresolved by architecture/runtime evidence.

The next implementation may apply exactly this narrow signing correction at
the XPC direct call boundary, with one source variable and a fresh clean
build. Do not generalize `MSFindSymbol` to sign every result, because many
uses are hook/patch inputs with different contracts. Do not add role gates,
remove setup, fabricate returns, or change TCC/pixels/input behavior until the
owned generation repeats successfully.
