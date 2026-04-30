# ModSecurity JSON/XML Processing – Security & Architecture Summary

## 🧠 Overall Conclusion

- XML and JSON are handled very differently in ModSecurity.
- This has **direct impact on memory usage and security**.
- A **JSON library alone is not sufficient for security**.
- A **dedicated control layer is required** to enforce limits and guarantee safe behavior.

---

## 🔍 1. XML vs JSON Processing

### XML

- Uses **libxml2**
- Combines:
  - **DOM (`xmlDoc`)** → full tree in memory
  - **SAX** → for ARGS extraction

**Issues:**
- ❌ No clearly enforced depth/node limits visible in analyzed code
- ❌ Potentially high memory usage due to DOM

**Risk:**
> Full tree construction may lead to memory exhaustion or DoS scenarios.

---

### JSON

- Uses **YAJL (event-based / streaming parser)**
- No DOM construction in ModSecurity code
- BUT:
  - Entire request body is buffered first

**Resulting model:**

Full request body in memory → streaming parsing on top

---

## 🛡️ 2. Existing Limits

### JSON – Implemented

- ✅ `SecRequestBodyJsonDepthLimit` → limits nesting depth
- ✅ `SecRequestBodyLimit` → limits total body size
- ✅ `SecArgumentsLimit` → limits extracted parameters

### JSON – Not Verified

- ❌ Maximum array size
- ❌ Maximum number of keys
- ❌ Maximum string length
- ❌ JSON-specific memory limits

---

### XML – More Problematic

- ❌ No explicit depth limit visible
- ❌ DOM tree is constructed
- ⚠️ Limits may apply too late

---

## ⚠️ 3. Key Insight

> The most critical factor is **when limits are enforced**.

GOOD:    during parsing → early abort BAD:     after parsing → too late

---

## 🔄 4. Replacing YAJL (JSON Library Change)

### Risks

- Switching to DOM/tree-based parser:
  - ❌ higher memory usage
  - ❌ limits applied too late
  - ❌ weaker DoS protection

### Requirements for Replacement

A new JSON library must:
- support **streaming/event-based parsing**
- allow **early abort during parsing**
- avoid building full JSON trees by default

---

## 📊 5. JSON Library Evaluation

### Recommended

- **RapidJSON**
  - SAX + optional DOM
  - streaming support
  - header-only

- **jsoncons**
  - feature-rich
  - streaming + schema support
  - more complex

---

### Use with Caution

- **nlohmann/json**
  - easy to use
  - ⚠️ default usage is DOM-based

- **simdjson**
  - very fast
  - different (lazy/on-demand) model
  - not a direct SAX replacement

---

### Less Suitable

- json-c
- Jansson
- cJSON
- JsonCpp
- yyjson
- Glaze

→ primarily DOM/object-based

---

## 🧩 6. Blaze (JSON Schema Validator)

> Blaze is **not a parser**.

### Suitable for:
- JSON Schema validation
- enforcing structure rules (types, required fields, etc.)

### Not suitable for:
- streaming parsing
- early-abort enforcement
- replacing YAJL

**Correct usage:**

Parser → Control Layer → (optional) Schema Validator (Blaze)

---

## 🏗️ 7. Required Architecture

[ HTTP Body ] ↓ [ Streaming JSON Parser ] ↓ [ Control Layer (custom logic) ] ↓ [ ModSecurity (ARGS, rules) ] ↓ [ optional: Schema Validator ]

---

## 🔥 8. Responsibilities of the Control Layer

### Must enforce:

- Maximum depth
- Number of keys / elements
- Array sizes
- String lengths
- Memory constraints

---

### Critical requirement: Early Abort

Parser → Callback → Control Logic → STOP immediately

> Without early abort, protection is significantly weakened.

---

### Additional tasks:

- Controlled JSON → ARGS mapping
- Prevent parameter explosion
- Maintain consistent WAF behavior

---

## ❌ 9. What to Avoid

- Replacing parser without architecture changes
- Using DOM-based parsing without strict limits
- Applying limits only after full parsing
- Assuming the library handles security

---

## 📌 Final Conclusion

- XML is currently **more risky** due to DOM usage and missing limits.
- JSON is **better protected**, but still incomplete.
- **Security does not come from the library.**
- **Security comes from a control layer enforcing limits during parsing.**

---

## 🧠 Key Takeaway

> A JSON library is just a parser.  
> **Security only exists if a control layer enforces limits and guarantees early abort behavior.**
