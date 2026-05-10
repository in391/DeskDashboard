"""Generate three short headlines from scraped news snippets with Gemini.

Process overview:
1. The caller first scrapes candidate news text from AllSides and passes a
    subset of those strings into this function.
2. This function creates a Google Gen AI client configured for Vertex AI
    using the project and location defined below.
3. The list of scraped text snippets is sent to the Gemini model as the
    request contents.
4. The system instruction constrains the model to select three important
    items and return them as short headlines in a fixed format.
5. The function returns the plain text response from the model.

Prerequisites:
- Install the Google Cloud CLI (`gcloud`).
- Set up Application Default Credentials before running this code, for
    example with `gcloud auth application-default login`, so Vertex AI can
    authenticate the request.
"""

from google import genai
import json
from html.parser import HTMLParser
import requests

URL = "https://www.allsides.com/unbiased-balanced-news"
INSTRUCTION = "You are an editor in a news organization. " \
"You are responsible for choosing important news and writing headlines. " \
"You have to write a headline for the news based on the given information. " \
"Pick three important headlines in the news. Do not write the news, just write a short headline. " \
"Each headline should be under 10 words. Use abbreviation. " \
"Format should be like this: headline1. headline2. headline3."

class FontBoldDivParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.capture_stack = []
        self.text_buffers = []
        self.results = []

    def handle_starttag(self, tag, attrs):
        attrs_dict = dict(attrs)

        def _class_tokens(value):
            if not value:
                return []
            return str(value).split()

        if tag == "div":
            class_tokens = _class_tokens(attrs_dict.get("class"))
            if "global-bias-label" in class_tokens:
                return
            elif "font-bold" in class_tokens:
                self.capture_stack.append(True)
                self.text_buffers.append([])
                return
        
        if tag == "a":
            class_tokens = _class_tokens(attrs_dict.get("class"))
            if "main-link" in class_tokens:
                self.capture_stack.append(True)
                self.text_buffers.append([])
                return

        if self.capture_stack:
            self.capture_stack.append(False)

    def handle_data(self, data):
        if self.capture_stack and self.capture_stack[-1] is True:
            self.text_buffers[-1].append(data)

    def handle_endtag(self, tag):
        if not self.capture_stack:
            return

        is_capture_node = self.capture_stack.pop()
        if is_capture_node:
            joined = " ".join(part.strip() for part in self.text_buffers.pop())
            cleaned = " ".join(joined.split())
            if cleaned:
                self.results.append(cleaned)


def crawl_font_bold_texts(url: str):
    response = requests.get(
        url,
        headers={"User-Agent": "Mozilla/5.0"},
        timeout=20,
    )
    response.raise_for_status()

    parser = FontBoldDivParser()
    parser.feed(response.text)
    return parser.results

def query_gemini_for_headlines(texts):
    client = genai.Client(vertexai=True, project='elegant-zodiac-386801', location='global')

    response = client.models.generate_content(
        model="gemini-3.1-flash-lite-preview",
        contents=texts,
        config=genai.types.GenerateContentConfig(
            system_instruction=INSTRUCTION,
        )
    )

    return response.text

def main():
    texts = crawl_font_bold_texts(URL)
    return query_gemini_for_headlines(texts[:12])

if __name__ == "__main__":
    print(main())