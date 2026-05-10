import ssl
import urllib.error
import urllib.request

try:
  import certifi
except ImportError:
  certifi = None


def fetch_text_with_ssl(url_or_request, timeout=10, resource_name="resource"):
  ssl_context = None
  if certifi is not None:
    ssl_context = ssl.create_default_context(cafile=certifi.where())

  try:
    with urllib.request.urlopen(url_or_request, timeout=timeout, context=ssl_context) as response:
      return response.read().decode("utf-8", errors="replace")
  except urllib.error.HTTPError:
    raise
  except urllib.error.URLError as error:
    reason = getattr(error, "reason", None)
    if not isinstance(reason, ssl.SSLCertVerificationError):
      raise

    print(f"SSL verification failed for {resource_name}. Retrying with insecure SSL context.")
    insecure_context = ssl._create_unverified_context()
    with urllib.request.urlopen(url_or_request, timeout=timeout, context=insecure_context) as response:
      return response.read().decode("utf-8", errors="replace")
