"""Check live subway operation status and return an alert string.

Process overview:
1. Request the train-status payload from the configured API endpoint.
2. Strip a possible UTF-8 BOM and parse the response as JSON.
3. Read the status fields used by this implementation (`check_dt`, `check_om`).
4. Evaluate those fields with `_ends_with_zero` to determine whether service
	is normal.
5. Return an empty string when operation is normal, or an alert message when
	delay/issue conditions are detected.

Important customization note:
- You must change both the parsed fields and the condition logic based on the
	API you access, because each provider uses different response keys and
	status codes.
"""

import json
import urllib.error

from http_request_ssl import fetch_text_with_ssl

# This module checks the current status of Tokyu trains by fetching data from their public API.
# Please change the URL and parsing logic based on your train service.
SUBWAY_STATUS_URL = "https://www.tokyu.co.jp/unten/unten2.json"

def _ends_with_zero(value):
	if value is None:
		return "Fetch Error"
	return str(value).split()[-1] == "0"

def main():
	try:
		response_text = fetch_text_with_ssl(
			SUBWAY_STATUS_URL,
			timeout=5,
			resource_name="Tokyu train status"
		)
		# Some upstream responses include a UTF-8 BOM, which breaks json.loads.
		payload = json.loads(response_text.lstrip("\ufeff"))
	except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
		print(f"Failed to fetch Tokyu status: {error}")
		return "Fetch Error"

	check_dt = payload.get("check_dt")
	check_om = payload.get("check_om")

	if _ends_with_zero(check_dt) and _ends_with_zero(check_om):
		return ""
	return "Train Delay Alert!"


if __name__ == "__main__":
	main()
