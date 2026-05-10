"""Fetch work/OOO status for the next few days from Google Calendar.

Process overview:
1. Load previously saved OAuth credentials from `token.json` if they exist.
2. If the token is missing or invalid, refresh it when possible or start the
    local Google OAuth login flow with `credentials.json`.
3. Save the updated access token so later runs can reuse it.
4. Build the Google Calendar API client with the authenticated credentials.
5. Query calendar events for the configured date window.
6. Fetch and parse holiday data from the holiday ICS source.
7. Initialize each day in the window with a default Work/OOO status based on
    weekday rules.
8. Override that status using holiday dates and matching calendar events.
9. Return the final per-day status map keyed by ISO date.

Prerequisites:
- The user must have valid Google API OAuth credentials for this operation.
- `credentials.json` must be present, and the first run will create
  `token.json` after the user authorizes access.
"""

import datetime
import os.path
import urllib.error

from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from http_request_ssl import fetch_text_with_ssl

# If modifying these scopes, delete the file token.json.
SCOPES = ["https://www.googleapis.com/auth/calendar.readonly"]
# URL for fetching Japanese holiday ICS data. This can be replaced with other sources if needed.
HOLIDAY_ICS_URL = "https://calendar.google.com/calendar/ical/en.japanese%23holiday%40group.v.calendar.google.com/public/basic.ics"

# Holiday ICS file can be downloaded from sources like:
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
holiday_ics_file_path = os.path.join(BASE_DIR, 'holiday.ics')
token_file_path = os.path.join(BASE_DIR, 'token.json')
credentials_file_path = os.path.join(BASE_DIR, 'credentials.json')

DATE_RANGE = 7

class CalendarDate:
  def __init__(self, date, status=None):
    if isinstance(date, datetime.datetime):
      self.date = date.date()
    else:
      self.date = date

    if status is not None:
      self.status = status
    elif self.date.weekday() >= 5:
      self.status = "OOO"
    else:
      self.status = "Work"

def get_status(date):
  return "OOO" if date.weekday() >= 5 else "Work"

def get_date(date_str):
  if date_str is None:
    return None

  try:
    # Handles values like "2026-04-18".
    return datetime.date.fromisoformat(date_str)
  except ValueError:
    pass

  try:
    # Handles values like "2026-04-18T13:00:00+09:00".
    return datetime.datetime.fromisoformat(date_str).date()
  except ValueError:
    return None

def apply_shift_status(event, status_by_day, status="Work"):
  event_start_raw = event["start"].get("dateTime", event["start"].get("date"))
  event_end_raw = event["end"].get("dateTime", event["end"].get("date"))
  event_start = get_date(event_start_raw)
  if event_end_raw.endswith("T00:00:00+09:00"):
    # Adjust end date for all-day events since Google Calendar's API returns the end date as the day after the event for all-day events
    event_end = get_date(event_end_raw)-datetime.timedelta(days=1)
  else:
    event_end = get_date(event_end_raw)

  if event_start is None or event_end is None:
    return

  event_dates = [
      event_start + datetime.timedelta(days=offset)
      for offset in range((event_end - event_start).days + 1)
  ]
  for date in event_dates:
    if date in status_by_day.keys():
      status_by_day[date] = status

def get_holiday_ics():
  try:
    with open(holiday_ics_file_path, 'r') as file_handle:
      ics_data = file_handle.read()
    # Check if the file contains next year's data by looking for the current year in the content
    current_year = datetime.datetime.now().year+1
    if str(current_year) not in ics_data:
      print(f"Holiday ICS file at {holiday_ics_file_path} does not contain data for the current year. Fetching updated ICS from {HOLIDAY_ICS_URL}")
      raise LookupError
    return ics_data
  except (FileNotFoundError, LookupError):
    print(f"Holiday ICS file not found or outdated at {holiday_ics_file_path}. Fetching from {HOLIDAY_ICS_URL}")
    ics_data = fetch_text_with_ssl(HOLIDAY_ICS_URL, timeout=10, resource_name="holiday ICS")
    with open(holiday_ics_file_path, 'w') as f:
      f.write(ics_data)
    print(f"Saved Holiday ICS to {holiday_ics_file_path}")
    return ics_data
  except Exception as error:
    print(f"Failed to fetch or read Holiday ICS: {error}")
    raise

def query_holidays_from_ics(start_datetime, end_datetime):
  start_date = start_datetime.date()
  end_date = end_datetime.date()
  print(f"Querying holidays from ICS between {start_date} and {end_date}")
  holiday_dates = set()
  try:
    ics_data = get_holiday_ics()
  except (urllib.error.URLError, TimeoutError) as error:
    print(f"Failed to fetch holiday ICS: {error}")
    return holiday_dates
  print(f"Processing ICS data for holidays")
  print(f"ICS data length: {len(ics_data)} characters")
  for line in ics_data.splitlines():
    if not line.startswith("DTSTART"):
      continue

    parts = line.split(":", 1)
    if len(parts) != 2:
      continue

    date_value = parts[1].strip()
    if len(date_value) < 8:
      continue

    try:
      holiday_date = datetime.datetime.strptime(date_value[:8], "%Y%m%d").date()
    except ValueError:
      continue

    if start_date <= holiday_date and holiday_date <= end_date:
      holiday_dates.add(holiday_date)
  return holiday_dates

def main():
  creds = None
  # The file token.json stores the user's access and refresh tokens, and is
  # created automatically when the authorization flow completes for the first
  # time.
  if os.path.exists(token_file_path):
    creds = Credentials.from_authorized_user_file(token_file_path, SCOPES)
  # If there are no (valid) credentials available, let the user log in.
  if not creds or not creds.valid:
    if creds and creds.expired and creds.refresh_token:
      creds.refresh(Request())
    else:
      flow = InstalledAppFlow.from_client_secrets_file(
          credentials_file_path, SCOPES
      )
      creds = flow.run_local_server(port=0)
    # Save the credentials for the next run
    with open(token_file_path, "w") as token:
      token.write(creds.to_json())

  try:
    service = build("calendar", "v3", credentials=creds)

    # Call the Calendar API
    now = datetime.datetime.now(tz=datetime.timezone.utc)
    start_of_today = now.replace(hour=0, minute=0, second=0, microsecond=0)
    end_of_window = start_of_today + datetime.timedelta(days=DATE_RANGE+1)
    print(f"Getting events for the next {DATE_RANGE} days (including today)")
    events_result = (
        service.events()
        .list(
            calendarId="primary",
            timeMin=start_of_today.isoformat(),
            timeMax=end_of_window.isoformat(),
            singleEvents=True,
            orderBy="startTime",
        )
        .execute()
    )
    events = events_result.get("items", [])

    # Holiday
    holiday_dates = query_holidays_from_ics(
      start_of_today,
      end_of_window
    )
    
    # Create a list of CalendarDate objects for the next DATE_RANGE days
    status_by_day = {
      datetime.date.today() + datetime.timedelta(days=offset):
      get_status(datetime.date.today() + datetime.timedelta(days=offset))
      for offset in range(DATE_RANGE)
    }

    # Override status to "OOO" for holidays
    for holiday_date in holiday_dates:
      if holiday_date in status_by_day.keys():
        status_by_day[holiday_date] = "OOO"

    # Override status based on calendar events (e.g. shifts, OOO)
    for event in events:
      if "shift" in event["summary"].lower():
          apply_shift_status(event, status_by_day, status="Work")
      elif "Out of office" in event["summary"]:
          apply_shift_status(event, status_by_day, status="OOO")

    result = {date.isoformat(): status for date, status in status_by_day.items()}
    return result

  except HttpError as error:
    print(f"An error occurred: {error}")


if __name__ == "__main__":
  main()