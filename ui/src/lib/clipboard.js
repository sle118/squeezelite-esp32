// Copying text from a page served by the device.
//
// The interface shell is served over plain HTTP (http://<device>), and browsers only
// expose navigator.clipboard in a secure context (HTTPS or localhost). On the device
// page the API is simply undefined, so a "copy" button has to fall back to the old
// textarea + execCommand trick. Returns true when the text reached the clipboard.

export async function copyText(text) {
  const value = String(text ?? '');
  if (!value) return false;

  if (window.isSecureContext && navigator.clipboard?.writeText) {
    try {
      await navigator.clipboard.writeText(value);
      return true;
    } catch {
      // fall through to the legacy path
    }
  }

  const area = document.createElement('textarea');
  area.value = value;
  area.readOnly = true;
  area.setAttribute('aria-hidden', 'true');
  // keep it out of view but still selectable
  area.style.cssText = 'position:fixed;top:0;left:0;width:1px;height:1px;padding:0;border:0;opacity:0';
  document.body.appendChild(area);

  let copied = false;
  try {
    area.select();
    area.setSelectionRange(0, area.value.length);
    copied = document.execCommand('copy');
  } catch {
    copied = false;
  } finally {
    area.remove();
  }
  return copied;
}
