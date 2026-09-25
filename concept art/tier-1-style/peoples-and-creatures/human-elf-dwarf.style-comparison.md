# Human, elf and dwarf: historical comparison provenance

Status: **Superseded by the owner-approved individual sheets on 2026-09-25.** The combined artwork has been removed. These notes retain historical generation provenance only; the overall art bible and twelve-peoples comparison checklist remain incomplete.

Current references: [Human — female](people.human_heartland_female.style-study.md), [Dwarf — male](people.dwarf_male.style-study.md), and [Elf — male](people.elf_male.style-study.md). Each has one standing figure and one matching headshot.

## Source and scope

- Style reference: [heartland human v2](people.human_heartland.style-study-v2.png), preferred by the owner.
- Catalogue identities: `people.human_heartland`, `people.elf`, `people.dwarf`.
- Anatomy and attire: `races.human`, `races.elf`, `races.dwarf` and their empty people deltas in [heritage-appearance.json](../../../generator/data/heritage-appearance.json).
- Subjects are illustrative individuals, not new catalogue identities. Their coloring, sex and clothing examples do not define an entire population.
- Human: adult woman, balanced anatomy, round ears, linen and wool, leather belt and pouch, low turnshoes.
- Elf: adult male, long narrow frame, long swept ears, deep-walnut skin, silver hair, wood circlet, natural drapery and wrapped footwear.
- Dwarf: adult male, short broad muscular frame, heavy brow and large nose, matching grey hair and full beard, dense protective layers and sturdy boots.

The intended standing heights are 1.62 m, 1.86 m and 1.27 m respectively, drawn from the female human and male elf/dwarf catalogue means. Other body types and sexes remain to be explored.

## Historical visual review

The sheet contains three full figures and three corresponding portraits, with readable labels. It maintains the chosen illustrated style while distinguishing the elf's narrow vertical silhouette and the dwarf's broad, compact silhouette. Human footwear now reads as low shoes.

A second generation pass adjusted the lower figures to improve relative height. The resulting image is still an illustrative comparison: perspective, pose and generated scaling are approximate, not a measured orthographic guide. Production sheets must use the actual catalogue measurements.

The near elf ear is clearly elongated and extends past the crown; the far ear is largely obscured. A profile and unobstructed rear view are still needed to validate ear attachment and full length. Drapery partly hides leg proportions.

Owner feedback on v1: "Dwarf came out a bit too realistic, but the elf and human are good".

v2 simplifies the dwarf's facial planes, creases, beard locks, arm shading and clothing texture into the same restrained anime rendering language. The large nose, heavy brow, full grey beard and broad adult silhouette remain. Visual inspection confirms a clearer illustrated treatment in both dwarf views. Human and elf designs and the composition remain consistent, although the image edit also introduced minor surface-rendering changes outside the dwarf region; it is not a pixel-identical composite.

Owner feedback on v2: "Alright that works". Its rendering direction carries forward in the current individual sheets: simplified facial planes, grouped hair and beard shapes, selective contour lines and restrained material texture, while preserving each people's distinct anatomy.

No checklist items are marked complete: the twelve-peoples comparison, full art bible and production anatomy sheets remain outstanding.

## Generation provenance

Generated using the built-in image generation tool, then revised with the same tool for relative figure scale and subsequently for dwarf stylization. Both combined artwork versions have now been superseded and removed. Historical prompts are retained below to document the source of the current individual sheets. No catalogue data was edited.

### Dwarf rendering revision (v2)

Edit target: the earlier comparison sheet, since removed from the repository.

Use case: style-transfer
Asset type: FWGC++ Tier 1 comparison sheet, targeted dwarf rendering correction.
Edit target: supplied human / elf / dwarf comparison sheet.
User feedback: "Dwarf came out a bit too realistic, but the elf and human are good".
CHANGE ONLY THE DWARF in the right-hand column: both the large upper-right portrait and the lower-right full-body figure. Treat the HUMAN and ELF columns as frozen, approved image regions: preserve their pixels, composition, figures, portraits, faces, costumes and rendering as closely as possible. Preserve all three labels, neutral paper background, image size, figure positions and relative heights.

Required change: redraw the dwarf in the SAME restrained anime illustration language as the human and elf. The present dwarf looks like a realistic fantasy painting; noticeably simplify him into designed, graphic painted forms with selective clear linework and controlled cel-painterly shadows. Use the HUMAN face and ELF face as internal style references only, NOT as anatomy references.

Dwarf face: keep exactly this broad, mature, muscular dwarf identity, large broad high-bridged nose, heavy brow shelf, small deep-set eyes with heavy lids, wide square head, small round ears, ruddy ochre skin. Use clear angular contour drawing, a few large warm facial color planes, one deliberate shadow shape under the brow, one broad nose-side shadow, clean readable cheek planes. Remove photographic skin grain, tiny creases, mottled pores and dense weathering. Indicate age with only two or three intentional brow/eye creases and a few sparse painted forge marks. Make the eyes slightly more graphically defined with crisp upper lids while keeping their small scale and deep setting. Retain warmth and adult gravitas. Do NOT make his eyes huge, his nose small, his face young or his anatomy humanlike.

Hair and beard: retain matching iron-grey scalp and FULL iron-grey beard, shaved temples, forked shape, two clan braids and gold beard rings. Redesign surface rendering as broad, distinct, sculpted locks with flat grouped highlights and a few confident dark strand accents, matching how the human's hair is drawn. Eliminate photoreal individual hairs, fuzzy stubble and finely rendered strand noise. Keep braid construction legible through simple repeated shapes.

Body and costume: preserve broad shoulders, thick forearms, short legs, almost no neck, existing pose, hands, dimensions and silhouette. Simplify exposed arms into restrained anime painted planes. Keep the existing leather/felt layers, fastenings and boots but reduce mottled surface noise, tiny scratches and dense microtexture. Broad controlled material shading with sharp selected seam accents, same level of detail as the human's clothes. Keep iron and gold muted, not glossy.

Critical: change rendering, NOT design, age, anatomy, scale or costume. The result should look like all three characters were illustrated by one artist for the same restrained anime fantasy game. No chibi, comic caricature, plastic 3D shading, doll features, photoreal skin, extra props, text or symbols.

### Initial prompt

Reference input: `people.human_heartland.style-study-v2.png`.

Use case: stylized-concept
Asset type: FWGC++ Tier 1 character art direction comparison sheet.
Primary request: Create an ORIGINAL human / elf / dwarf comparison sheet in precisely the restrained anime-painterly style of the supplied reference image. The owner selected this reference's balance of anime and grounded fantasy. Match its illustrated facial planes, selective contour lines, grouped hair highlights, elegant painted shadows and tactile simplified materials. Keep the painting visibly illustrated, never photographic. This is one cohesive art sheet of three peoples, not a game screenshot.
Input image role: STYLE REFERENCE and identity reference for the HUMAN woman only. Reuse her olive skin, brown eyes, tied dark brown hair and practical earth-toned clothing. Do not copy her face or body proportions onto the elf or dwarf.

Layout: Wide landscape production concept sheet, approximately 3:2, clean pale warm-grey painted paper, ample margins. THREE equally wide columns in this exact left-to-right order: HUMAN, ELF, DWARF. Each column has a small enlarged matching head-and-shoulders portrait at the top and one full-body standing character below. EXACTLY three full-body figures and three matching portraits total. All full-body feet rest on the SAME horizontal baseline; orthographic-like three-quarter views, identical camera scale and no foreshortening. Whole figures fully visible including feet and ears. The dwarf is markedly shorter than both others. Portraits are separate detail studies at comparable portrait framing, not additional full-size people. Each portrait must visibly match its own figure. Add only the simple understated labels "HUMAN", "ELF", "DWARF" above their columns. No other text, scales, numbers or grids. No scenic background, weapons or props beyond clothing accessories.

HUMAN / people.human_heartland: Adult woman from reference, standing height 1.62 m; balanced natural build around 7.4 heads tall. Round ordinary ears, expressive natural-sized brown almond eyes, actual nose bridge, mature cheeks, a few freckles, subtle signs of sun and work. Olive complexion. Practical linen tunic and wool cloak, tanned leather belt and pouch, iron pin. Her cloak must leave enough of the body silhouette visible to compare anatomy. IMPORTANT footwear correction: simple low soft brown leather medieval turnshoes with thin flexible soles, not laced ankle boots, not heavy boots. Hair in a loose dark brown practical tie. Restrained friendly composure.

ELF / people.elf: Adult male, standing height 1.86 m, distinctly taller than the human but NOT gigantic. Very lithe long build, approximately 8.1 heads tall, narrow shoulders, long neck, long legs, fine hands. Minimal sexual dimorphism: elegant and androgynous rather than broad-jawed. Face is long through the midface, high broad cheekbones tapering into a small chin; wide-set upturned almond eyes with flat amber-gold irises and little white below them. Smooth deep-walnut skin with a cool undertone, no freckles. Straight narrow quiet nose, fine high brows, thin defined closed lips; no beard. Long pale-silver fine straight hair and two small temple braids, styled behind ears. SIGNATURE EARS: both ears fully visible, exceptionally long, each a full hand's length from lobe to tip, set LOW on the skull and swept diagonally backward and UPWARD so the tips visibly rise PAST the crown of the head. Thin delicate ear edges lit translucently. These are anatomical ears, NOT horns or antennae; show attachment clearly. Never short horizontal elf ears. Single-layer naturally undyed woven bark-silk drapery, subtle spider-silk softness, carved living-wood circlet, a small horn clasp, soft wrapped footwear. Long flowing vertical garment silhouette but a modest side opening reveals leg length. No armor, metal crown, gold jewelry or ornate embroidery.

DWARF / people.dwarf: Adult male standing height 1.27 m, about 78 percent of human height and 68 percent of elf height, approximately 4.6 heads tall. Dense broad muscular adult, NOT chibi, NOT a child, NOT just a scaled-down human. Short strong legs, huge chest and thick powerful forearms, high-carried mass, wide trapezoid silhouette and almost no visible neck. Broad short square face, heavy continuous brow shelf, deep-set SMALL eyes with heavy lids, LARGE wide high-bridged nose joining the brow without a dip, broad jaw. Small thick round ears. Ruddy ochre weathered skin, stylized deep creases and sparse forge spark-burns. Iron-grey thick scalp hair and FULL matching iron-grey beard; shaved temples, forked beard with clan braids and plain beard rings. Beard and scalp must be the same grey. Heavy wool-felt and boiled-leather clothing, restrained riveted metal reinforcement and subtle gold-wire fastenings, sturdy hobnailed boots. The clothing must reveal the broad shoulders and forearms, no giant fantasy pauldrons. Solid adult presence and quiet warmth rather than comic grumpiness.

Art direction: These peoples have DIFFERENT anatomy but exactly the SAME rendering language. Anime influence must not homogenize their faces or erase the dwarf's nose/brow or elf's long ears. Natural diffuse light from upper left for every figure, coherent softly colored shadows, controlled earth tones and luminous complexion colors. Clear silhouettes that remain legible when reduced. Keep detail concentrated on faces, hands and material edges. No glossy doll skin, huge sparkling eyes, photoreal pores, chibi, exaggerated sexualization, modern techwear, excessive ornament, watermark or logos. Study draft, not a final model sheet; the three individuals are examples of existing catalogue peoples, not new named identities.

### Revision prompt

Input: the initial generated comparison sheet.

Use case: precise-object-edit
Edit this original FWGC++ human/elf/dwarf art direction sheet. Change ONLY the scale and vertical placement of the three LOWER full-body figures. Keep the ENTIRE top row of portraits, all labels, identities, faces, costumes, colors, rendering style, background, image dimensions and left-to-right order unchanged.
This is a physical height comparison at identical camera scale. All three standing figures must share the same bottom foot baseline. The elf's BODY height (sole to scalp, excluding projecting ears) must be 1.148 times the human body height. The dwarf's body height (sole to scalp) must be 0.784 times the human body height. Keep the lower elf figure essentially its current size and position. Uniformly reduce the LOWER human figure to approximately 94% of its current height, and uniformly reduce the LOWER dwarf figure to approximately 87% of its current height, anchoring both to the same ground baseline as the elf. Do not stretch or squash their anatomy. Do not move the portraits or use perspective to change height. Preserve the empty spaces created above the smaller figures. The dwarf must remain a broad muscular adult with a heavy nose and brow, not chibi. The elf remains narrow and long-limbed. No new text, labels, scale marks or other additions. This is a narrowly targeted scale correction; preserve the illustration.
