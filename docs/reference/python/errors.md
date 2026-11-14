# Errors

The exceptions dexpp raises when it cannot use a file or a model. They all
derive from `dexpp.Error`, and `dexpp.Error` is a `ValueError`. See
[errors in Concepts](../../guide/concepts.md#errors) and
[errors in the editing guide](../../guide/editing-dex.md#errors).

A file problem is not one of these. A path that does not exist raises the
built-in `FileNotFoundError`. A path that exists but cannot be read (a
directory, a file without read permission) and a file that cannot be written
raise `OSError`.

::: dexpp.Error

::: dexpp.InvalidApkError

::: dexpp.InvalidDexError

::: dexpp.UnsupportedDexError

::: dexpp.ModelError

::: dexpp.InvalidModelError

::: dexpp.PoolOverflowError

::: dexpp.VersionTooLowError
