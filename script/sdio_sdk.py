"""Compile the reviewed Teensy SDIO acknowledgement fix without changing the SDK cache."""
import hashlib
from pathlib import Path

SDK_SHA256 = "09d469a55d6b6a42ce6376c8d7bd172259518e084b0adadf4af6733854f47f4a"

def apply(text):
    """Match the complete reviewed source and four unique replacement sites."""
    if hashlib.sha256(text.encode()).hexdigest() != SDK_SHA256:
        raise RuntimeError("SDIO adapter requires SDK review: SdioTeensy.cpp changed")
    replacements = [
        (
            """  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;
#if defined(__IMXRT1062__)
  SDHC_MIX_CTRL""",
            """  const uint32_t irqstat = SDHC_IRQSTAT;
  m_irqstat |= irqstat;
  SDHC_IRQSTAT = irqstat;
#if defined(__IMXRT1062__)
  SDHC_MIX_CTRL""",
        ),
        (
            """  SDHC_XFERTYP = xfertyp;
  if (waitTimeout(isBusyCommandComplete)) {""",
            """  // A fresh command must not observe the previous completion latch.
  SDHC_IRQSTAT = SDHC_IRQSTAT_CC | SDHC_IRQSTAT_CMD_ERROR;
  m_irqstat = 0;
  SDHC_XFERTYP = xfertyp;
  if (waitTimeout(isBusyCommandComplete)) {""",
        ),
        (
            """  m_irqstat = SDHC_IRQSTAT;
  SDHC_IRQSTAT = m_irqstat;

  return (m_irqstat & SDHC_IRQSTAT_CC) &&
         !(m_irqstat & SDHC_IRQSTAT_CMD_ERROR);""",
            """  // The ISR may already have consumed CC. Retain its status, and
  // acknowledge only command bits: TC and data errors belong to the DMA ISR.
  uint32_t primask;
  __asm__ volatile("mrs %0, primask" : "=r"(primask));
  __disable_irq();
  const uint32_t irqstat = SDHC_IRQSTAT | m_irqstat;
  SDHC_IRQSTAT = irqstat & (SDHC_IRQSTAT_CC | SDHC_IRQSTAT_CMD_ERROR);
  m_irqstat = irqstat;
  if (!primask) { __enable_irq(); }

  return (irqstat & SDHC_IRQSTAT_CC) &&
         !(irqstat & SDHC_IRQSTAT_CMD_ERROR);""",
        ),
        (
            """  return !(SDHC_IRQSTAT & (SDHC_IRQSTAT_CC | SDHC_IRQSTAT_CMD_ERROR));""",
            """  return !((SDHC_IRQSTAT | m_irqstat) &
           (SDHC_IRQSTAT_CC | SDHC_IRQSTAT_CMD_ERROR));""",
        ),
    ]
    for old, new in replacements:
        if text.count(old) != 1:
            raise RuntimeError("SDIO adapter replacement site changed")
        text = text.replace(old, new)
    return text


def configure(env):
    sdk = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy"))
    source = sdk / "libraries/SdFat/src/SdCard/SdioTeensy.cpp"
    contents = apply(source.read_text(encoding="utf-8"))
    target = Path(env.subst("$BUILD_DIR")) / "oc_sdio_sdk.cpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text(encoding="utf-8") != contents:
        target.write_text(contents, encoding="utf-8", newline="\n")

    def replace_sdio_source(env, node):
        if Path(node.srcnode().get_abspath()).resolve() != source.resolve():
            return node
        # Only this translation unit needs the original sibling headers.
        return env.Object(
            target=str(target.with_suffix(".o")), source=str(target),
            CPPPATH=list(env.get("CPPPATH", [])) + [str(source.parent)],
        )[0]

    env.AddBuildMiddleware(replace_sdio_source, "*SdioTeensy.cpp")


if "Import" in globals():
    Import("env")
    configure(env)
