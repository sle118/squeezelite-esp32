param (
    [Parameter(Position=0, Mandatory=$false)]
    [ValidateSet("t", "u", "d")]
    [string]$option
)

# Get the current directory
$currentDir = Get-Location

# Define target directories
$targetDir = "components\wifi-manager\webapp"
$distDir = "$targetDir\dist"

# Get list of files from the 'dist' directory
$fsFiles = Get-ChildItem -Recurse $distDir -File | ForEach-Object {
    $_.FullName.Substring($currentDir.Path.Length + 1).Replace("\", "/")
}

# Define additional files to include
$additionalFiles = @("webpack.c", "webpack.h", "webapp.cmake")

# Check if additional files exist in $targetDir and format them
$additionalFilesFormatted = @()
Get-ChildItem $targetDir -File | ForEach-Object {
    if ($additionalFiles -contains $_.Name) {
        $formatted = $_.FullName.Substring($currentDir.Path.Length + 1).Replace("\", "/")
        $additionalFilesFormatted += $formatted
        Write-Host "Found $formatted"
    }
}

# Get list of files from the Git index
$indexFiles = git ls-files -s $distDir | ForEach-Object {
    ($_ -split "\s+")[3]
}

# Combine and remove duplicates
$allFiles = $fsFiles + $additionalFilesFormatted + $indexFiles | Sort-Object -Unique
# ... (previous code remains unchanged)

# Apply the git command based on the option
$allFiles | ForEach-Object {
    $relativePath = $_
    $isInIndex = $indexFiles -contains $relativePath

    if ($null -eq $option) {
        $gitStatus = & git status --porcelain -- $relativePath
        if ($gitStatus) {
            $status = ($gitStatus -split "\s")[0]
            Write-Host "$relativePath has Git status: $status"
        } else {
            Write-Host "$relativePath is not tracked"
        }
    }
    elseif ($isInIndex) {
        if ($option -eq "d") {
            $resetResult = & git reset -- $relativePath 2>&1
            if ($resetResult -match 'error:') {
                Write-Host "Error resetting ${relativePath}: $resetResult"

                continue
            }
            $checkoutResult = & git checkout -- $relativePath 2>&1
            if ($checkoutResult -match 'error:') {
                Write-Host "Error checking out ${relativePath}: $checkoutResult"

                continue
            }
            Write-Host "Discarded changes in $relativePath"
        }
        # ... (rest of the code remains unchanged)
    }
    # else {
    #     # if ($option -eq "d") {
    #     #     Remove-Item -Path $relativePath -Force
    #     #     Write-Host "Removed untracked file $relativePath"
    #     # } else {
    #     #     Write-Host "File $relativePath is not tracked."
    #     # }
        
    # }
    else {
        if ($option -eq "t") {
            git add $relativePath
            git update-index --no-skip-worktree $relativePath
            Write-Host "Started tracking changes in $relativePath"
        } else {
            Write-Host "File $relativePath is not tracked."
        }
    }
}
