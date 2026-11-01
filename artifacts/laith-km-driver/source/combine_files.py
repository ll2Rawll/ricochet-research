import os

def combine_source_files():
    # The name of the output text file
    output_filename = "combined_source.txt"
    
    # Extensions to look for
    target_extensions = ('.cpp', '.h', '.hpp')

    # Get the current working directory
    current_dir = os.getcwd()

    print(f"Scanning directory: {current_dir}...")

    with open(output_filename, "w", encoding="utf-8") as outfile:
        # os.walk goes through the current folder and all sub-folders
        for root, dirs, files in os.walk(current_dir):
            
            # Skip the output file itself if it exists in the loop
            if output_filename in files:
                files.remove(output_filename)

            for file in files:
                if file.endswith(target_extensions):
                    file_path = os.path.join(root, file)
                    
                    # Create a nice header for the file
                    separator = "=" * 50
                    header = f"\n{separator}\nFILE NAME: {file}\nPATH: {file_path}\n{separator}\n\n"
                    
                    # Write the header
                    outfile.write(header)
                    
                    try:
                        # Open the source file and read it
                        # errors='replace' prevents crashes on weird characters
                        with open(file_path, "r", encoding="utf-8", errors='replace') as infile:
                            outfile.write(infile.read())
                            outfile.write("\n") # Add a newline at the end of the file content
                            print(f"Added: {file}")
                    except Exception as e:
                        print(f"Could not read {file}: {e}")
                        outfile.write(f"\n[ERROR READING FILE: {e}]\n")

    print(f"\nDone! All files merged into '{output_filename}'")

if __name__ == "__main__":
    combine_source_files()