# Synthetic LPR test resource

`lpr-test.png` is an AI-generated software test card, not a camera capture.
It was created on 2026-11-19 using the built-in image_gen tool (imagegen skill).
No reference images, vehicle logos or third-party image assets were supplied.
The image displays TEST IMAGE, NOT A REAL CAPTURE and the synthetic plate TEST123.
The full car card is used only for VehicleImage and VehicleImageURI.

Generation prompt:

> Create a simple clean synthetic test fixture image for a software camera emulator, landscape 768x512. Front view of a generic unbranded blue passenger car on a neutral grey background. A large white license plate reads exactly TEST123. Large legible black text banner at top reads TEST IMAGE and below it NOT A REAL CAPTURE. Clear flat illustration, no logos, no people, no real location, minimal details. Must be recognizably artificial testing image. Save output for use as project resource.

The delivered PNG is 1536x1024. It is stored unchanged from the generated output.

`lpr-plate-test.png` is the separate plate-only companion fixture, used for
Image and ImageURI. It was generated using the built-in image_gen tool from
`lpr-test.png` as the reference on 2026-11-19. It depicts the synthetic plate
TEST123 with a TEST IMAGE label, rather than the entire car.

Plate image edit prompt:

> Create a separate license-plate recognition test fixture from the provided synthetic vehicle image. Show ONLY a tightly framed rectangular white license plate with black border and the exact large characters TEST123, matching the plate in the reference. No whole car, no headlights, no windshield. Wide horizontal close-up plate composition. Include a small but clearly legible TEST IMAGE label below the plate to identify this as synthetic test data. Neutral background, crisp black text. This is the plate-only companion asset to the existing vehicle image; preserve that plate text exactly.

The delivered plate PNG is 2172x724 and is stored unchanged from the generated output.

## Additional vehicle pairs

The following six standalone PNGs were generated on 2026-11-19 with the built-in image_gen tool, without reference images. They are synthetic test fixtures, not camera captures. Each full vehicle and its separate plate use matching TEST numbers. Outputs were copied unchanged into this directory.

### lpr-test-TEST456.png

Prompt:

> Synthetic software camera test fixture: front view of one generic unbranded red SUV, complete vehicle visible, neutral grey background, clean flat illustration matching a software test card. Large legible white license plate reads exactly TEST456. Large heading TEST IMAGE and subtitle NOT A REAL CAPTURE. No logos, people or real location. Landscape composition. This is a standalone vehicle image, not a collage.

### lpr-plate-test-TEST456.png

Prompt:

> Synthetic license plate recognition software test fixture. Show ONLY one large rectangular white license plate with a thick black border and exact black characters TEST456. Tightly framed wide horizontal close-up, no car or vehicle parts. Clearly legible smaller label TEST IMAGE below the plate, neutral grey background. Crisp clean flat illustration. One standalone plate, not a collage.

### lpr-test-TEST789.png

Prompt:

> Synthetic software camera test fixture: front view of one generic unbranded white cargo van, complete vehicle visible, neutral grey background, clean flat illustration matching a software test card. Large legible white license plate reads exactly TEST789. Large heading TEST IMAGE and subtitle NOT A REAL CAPTURE. No logos, people or real location. Landscape composition. This is a standalone vehicle image, not a collage.

### lpr-plate-test-TEST789.png

Prompt:

> Synthetic license plate recognition software test fixture. Show ONLY one large rectangular white license plate with a thick black border and exact black characters TEST789. Tightly framed wide horizontal close-up, no car or vehicle parts. Clearly legible smaller label TEST IMAGE below the plate, neutral grey background. Crisp clean flat illustration. One standalone plate, not a collage.

### lpr-test-TEST321.png

Prompt:

> Synthetic software camera test fixture: front view of one generic unbranded yellow minibus, complete vehicle visible, neutral grey background, clean flat illustration matching a software test card. Large legible white license plate reads exactly TEST321. Large heading TEST IMAGE and subtitle NOT A REAL CAPTURE. No logos, people or real location. Landscape composition. This is a standalone vehicle image, not a collage.

### lpr-plate-test-TEST321.png

Prompt:

> Synthetic license plate recognition software test fixture. Show ONLY one large rectangular white license plate with a thick black border and exact black characters TEST321. Tightly framed wide horizontal close-up, no car or vehicle parts. Clearly legible smaller label TEST IMAGE below the plate, neutral grey background. Crisp clean flat illustration. One standalone plate, not a collage.

## Motorcycle pair

These two standalone synthetic PNGs were created with the built-in image_gen tool on 2026-11-19, without reference images, and copied unchanged into this directory. The rear-view green motorcycle and separate plate both show TEST654.

### lpr-test-TEST654.png

Prompt:

> Synthetic software camera test fixture: rear view of one generic unbranded green motorcycle, complete motorcycle visible without rider, neutral grey background, clean flat illustration matching a software test card. Large legible white rear license plate reads exactly TEST654. Large heading TEST IMAGE and subtitle NOT A REAL CAPTURE. No logos, people or real location. Landscape composition. One standalone motorcycle, not a collage.

### lpr-plate-test-TEST654.png

Prompt:

> Synthetic license plate recognition software test fixture. Show ONLY one rectangular white motorcycle license plate with a thick black border and exact black characters TEST654. Tightly framed horizontal close-up, no motorcycle or vehicle parts. Clearly legible smaller label TEST IMAGE below the plate, neutral grey background. Crisp clean flat illustration. One standalone plate, not a collage.

## Synthetic face test resources

`face-test-001.png`, `face-test-002.png`, and `face-test-003.png` were generated
on 2026-11-19 with the built-in image_gen tool using the imagegen skill. They
show fictional people and contain TEST IMAGE / SYNTHETIC FACE 001–003 labels.
No reference photographs or third-party image assets were supplied. Each output
was copied unchanged to this directory. The emulator uses each card as both the
recognized image and its matching reference; these are software fixtures.

Prompt template (one independent generation per subject):

> Use case: stylized-concept. Asset type: standalone face recognition software test fixture PNG. A clearly artificial clean illustrated headshot of {subject}, straight-on centered face against plain grey background. No real person or reference. Square composition, head and shoulders. Large legible banner text exactly "TEST IMAGE" and below "SYNTHETIC FACE {number}". No brands or logos. One portrait only.

Subjects and numbers:

- 001: a fictional adult woman with short dark hair
- 002: a fictional adult man with curly hair and glasses
- 003: a fictional adult woman with silver hair
