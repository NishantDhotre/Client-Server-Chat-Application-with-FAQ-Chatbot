import mmap
import contextlib
# import torch
from transformers import AutoTokenizer, AutoModelForCausalLM

# Initialize the model and tokenizer
model_name = "openai-community/gpt2-medium"
tokenizer = AutoTokenizer.from_pretrained(model_name)
model = AutoModelForCausalLM.from_pretrained(model_name)

# Set device to GPU if available
# device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
model
# .to(device)

# Define the name and size of the shared memory
SHM_NAME = "chatbot_shm"
SHM_SIZE = 4096

# Function to generate a response using the GPT-2 model
def generate_response(prompt):
    input_ids = tokenizer.encode(prompt, return_tensors='pt')
    # .to(device)
    output = model.generate(
        input_ids,
        max_new_tokens=30,  # Number of tokens to generate
        pad_token_id=tokenizer.eos_token_id,
        no_repeat_ngram_size=3,
        num_return_sequences=1,
        top_p=0.95,
        temperature=0.1,
        do_sample=True
    )
    response = tokenizer.decode(output[0], skip_special_tokens=True)
    return response

# Function to read from and write to the shared memory
def process_shared_memory():
    # Open the shared memory object
    with open(f"/dev/shm/{SHM_NAME}", "r+b") as shm:
        with mmap.mmap(shm.fileno(), SHM_SIZE, access=mmap.ACCESS_READ | mmap.ACCESS_WRITE) as mem:
            # Wait for input data to be available
            while True:
                # Assuming the first byte indicates if new data is available
                while mem[0] != ord('1'):
                    pass  # Busy wait for the server to write new data

                # Read the prompt from the shared memory
                prompt = mem[1:].split(b'\x00')[0].decode('utf-8')

                # Generate the response using the GPT-2 model
                response = generate_response(prompt)

                # Write the response to shared memory
                mem.seek(0)  # Go to the beginning
                mem[0] = ord('2')  # Indicate that the response is being written
                mem[1:len(response)+1] = response.encode('utf-8')
                mem[len(response)+1] = b'\x00'  # Null-terminate the string

                # Indicate to the server that the response is ready
                mem[0] = ord('3')

if __name__ == "__main__":
    process_shared_memory()
