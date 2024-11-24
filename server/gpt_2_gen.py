import time
import torch
from transformers import AutoTokenizer, AutoModelForCausalLM

start = time.time()
# Load pre-trained GPT-2 model and tokenizer
#model_name = "gpt2"
model_name = "openai-community/gpt2-medium"  # You can choose from "gpt2", "gpt2-medium", "gpt2-large", "gpt2-xl"
tokenizer = AutoTokenizer.from_pretrained(model_name)
model = AutoModelForCausalLM.from_pretrained(model_name)

# Set device to GPU if available
device = "cuda" if torch.cuda.is_available() else "cpu"
model.to(device)
print(device)

# Generate text based on a prompt (same generate_text() function as before)
def generate_text(prompt, max_length=80):
    input_ids = tokenizer.encode(prompt, return_tensors="pt").to(device)
    output = model.generate(input_ids, max_new_tokens = 30, pad_token_id=tokenizer.eos_token_id, no_repeat_ngram_size=3, num_return_sequences=1, top_p = 0.95, temperature = 0.1, do_sample = True)
    generated_text = tokenizer.decode(output[0], skip_special_tokens=True)
    return generated_text

# Example prompt
prompt = """You are a Chatbot capable of answering questions for airline services. Please respond the following user question posed by the user to the best of your knowledge and do not generate any follow up questions.
User > Hello, how are you?
Chatbot Answer > """

generated_text = generate_text(prompt)
end = time.time()
print(f'Time taken = {end - start}')
# Print the generated text
print("Generated Text:")
print(generated_text)

